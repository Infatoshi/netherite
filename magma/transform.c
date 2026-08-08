/* transform.c - cr_transform: world CrVertex[] -> screen-space CrScreenTri[].
 *
 * Pipeline per input triangle:
 *   1. MVP = perspective(fov, aspect) * view(cam).  Model is identity (verts are world).
 *   2. Transform each vertex to clip space (CrVec4).
 *   3. Near-plane clip IN CLIP SPACE, before the perspective divide, against the plane
 *      z + w = 0 (keep the half-space z + w >= 0). Sutherland-Hodgman on the single
 *      near plane yields a polygon of 3 or 4 verts (or 0), fan-triangulated to 0/1/2 tris.
 *   4. Perspective divide, viewport map to pixel space, fill CrScreenVert.
 *
 * Handedness / conventions live in core/math.c's header. The camera looks down -Z in
 * eye space; NDC z in [-1,1] maps to depth [0,1]; screen y is flipped (y=0 at top).
 * Aspect uses the framebuffer dimensions (fb_w/fb_h are authoritative, per the task),
 * NOT cam->aspect.
 */
#include "core/types.h"
#include <math.h>
#include <stddef.h>

/* Working vertex during clipping: clip-space position + interpolable attributes. */
typedef struct {
    CrVec4 clip;
    CrVec2 uv;
    float  light;
    float  ao;
    float  eye_dist;
    float  tint[4]; /* r,g,b,a as float for lerping */
    float  blk;     /* block light level (lightmap-coord mode) */
} ClipVert;

/* All helpers + the per-triangle worker are CR_HD: cuda/raster_cuda.cu
 * #includes this file under private names (the core/shade.c pattern) so the
 * GPU transform kernel runs byte-identical source. Numerical contract as in
 * shade.c: no FMA (-ffp-contract=off / --fmad=false), fixed operation order. */
static CR_HD ClipVert make_clipvert(CrMat4 mvp, const CrVertex *v, CrVec3 campos)
{
    ClipVert cv;
    CrVec4 p = { v->pos.x, v->pos.y, v->pos.z, 1.0f };
    cv.clip = cr_mat4_mul_vec4(mvp, p);
    cv.uv = v->uv;
    cv.light = v->light;
    cv.ao = v->ao;
    cv.blk = v->blk;
    float dx = v->pos.x - campos.x;
    float dy = v->pos.y - campos.y;
    float dz = v->pos.z - campos.z;
    cv.eye_dist = sqrtf(dx * dx + dy * dy + dz * dz);
    cv.tint[0] = (float)v->tint.r;
    cv.tint[1] = (float)v->tint.g;
    cv.tint[2] = (float)v->tint.b;
    cv.tint[3] = (float)v->tint.a;
    return cv;
}

/* signed distance to the near plane z + w = 0; inside (kept) when >= 0. */
static CR_HD float near_dist(const ClipVert *v) { return v->clip.z + v->clip.w; }

static CR_HD ClipVert lerp_clipvert(const ClipVert *a, const ClipVert *b, float t)
{
    ClipVert r;
    r.clip.x = a->clip.x + t * (b->clip.x - a->clip.x);
    r.clip.y = a->clip.y + t * (b->clip.y - a->clip.y);
    r.clip.z = a->clip.z + t * (b->clip.z - a->clip.z);
    r.clip.w = a->clip.w + t * (b->clip.w - a->clip.w);
    r.uv.x = a->uv.x + t * (b->uv.x - a->uv.x);
    r.uv.y = a->uv.y + t * (b->uv.y - a->uv.y);
    r.light = a->light + t * (b->light - a->light);
    r.ao = a->ao + t * (b->ao - a->ao);
    r.blk = a->blk + t * (b->blk - a->blk);
    r.eye_dist = a->eye_dist + t * (b->eye_dist - a->eye_dist);
    for (int i = 0; i < 4; ++i)
        r.tint[i] = a->tint[i] + t * (b->tint[i] - a->tint[i]);
    return r;
}

/* Sutherland-Hodgman clip of a triangle against near plane z+w>=0.
 * Writes up to 4 output verts to `out`, returns the count (0, 3, or 4). */
static CR_HD int clip_near(const ClipVert in[3], ClipVert out[4])
{
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVert *cur = &in[i];
        const ClipVert *nxt = &in[(i + 1) % 3];
        float dc = near_dist(cur);
        float dn = near_dist(nxt);
        int cur_in = dc >= 0.0f;
        int nxt_in = dn >= 0.0f;
        if (cur_in)
            out[n++] = *cur;
        if (cur_in != nxt_in) {
            float t = dc / (dc - dn);
            out[n++] = lerp_clipvert(cur, nxt, t);
        }
    }
    return n;
}

/* Perspective divide + viewport map + fill CrScreenVert. */
static CR_HD CrScreenVert to_screen(const ClipVert *cv, int fb_w, int fb_h)
{
    CrScreenVert sv;
    float invw = 1.0f / cv->clip.w;
    float ndc_x = cv->clip.x * invw;
    float ndc_y = cv->clip.y * invw;
    float ndc_z = cv->clip.z * invw;

    sv.spos.x = (ndc_x * 0.5f + 0.5f) * (float)fb_w;
    sv.spos.y = (0.5f - ndc_y * 0.5f) * (float)fb_h; /* y flipped: y=0 at top */
    sv.spos.z = ndc_z * 0.5f + 0.5f;                 /* [-1,1] -> [0,1] */

    sv.invw = invw;
    sv.uv_w.x = cv->uv.x * invw;
    sv.uv_w.y = cv->uv.y * invw;
    sv.light_w = cv->light * invw;
    sv.ao_w = cv->ao * invw;
    sv.blk_w = cv->blk * invw;
    sv.eye_dist_w = cv->eye_dist * invw;
    /* Smooth vertex color (LayerEnderDragonDeath); attr * invw. */
    sv.tint_r_w = cv->tint[0] * invw;
    sv.tint_g_w = cv->tint[1] * invw;
    sv.tint_b_w = cv->tint[2] * invw;
    sv.tint_a_w = cv->tint[3] * invw;
    return sv;
}

/* With the y-down viewport map above, front faces have negative signed area.
 * Keep exact degenerates in the transform output so the rasterizer retains
 * its existing zero-area handling. */
#define CR_BACKFACE_EPSILON 0.0f
static CR_HD int cr_screen_backface(const CrScreenTri *tri)
{
    float x0 = tri->v[0].spos.x, y0 = tri->v[0].spos.y;
    float x1 = tri->v[1].spos.x, y1 = tri->v[1].spos.y;
    float x2 = tri->v[2].spos.x, y2 = tri->v[2].spos.y;
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    return area > CR_BACKFACE_EPSILON;
}

/* One input triangle (3 consecutive CrVertex) -> 0..2 screen tris (near clip
 * fan). Shared per-triangle worker for the CPU loop below AND the CUDA
 * transform kernel (cuda/raster_cuda.cu #includes this file). */
CR_HD int cr_transform_tri(CrMat4 mvp, CrVec3 campos, const CrVertex *v3,
                           int fb_w, int fb_h, CrScreenTri out[2])
{
    ClipVert tri[3];
    for (int k = 0; k < 3; ++k)
        tri[k] = make_clipvert(mvp, &v3[k], campos);

    ClipVert poly[4];
    int np = clip_near(tri, poly);
    if (np < 3)
        return 0; /* fully behind near plane */

    /* fan triangulate poly[0], poly[i], poly[i+1] */
    int n = 0;
    for (int i = 1; i + 1 < np && n < 2; ++i) {
        CrScreenTri st;
        st.v[0] = to_screen(&poly[0], fb_w, fb_h);
        st.v[1] = to_screen(&poly[i], fb_w, fb_h);
        st.v[2] = to_screen(&poly[i + 1], fb_w, fb_h);
        out[n++] = st;
    }
    return n;
}

int cr_transform(const CrVertex *verts, int nverts,
                 const u32 *idx, int nidx,
                 const CrCamera *cam, int fb_w, int fb_h,
                 CrScreenTri *out, int max_out)
{
    if (!verts || !cam || !out || max_out <= 0 || fb_w <= 0 || fb_h <= 0)
        return 0;

    float aspect = (float)fb_w / (float)fb_h; /* fb dims authoritative */
    CrMat4 proj = cr_perspective(cam->fov_deg, aspect, cam->znear, cam->zfar);
    CrMat4 view = cr_camera_view(cam);
    CrMat4 mvp = cr_mat4_mul(proj, view);

    int ntris = (idx != NULL) ? (nidx / 3) : (nverts / 3);
    int count = 0;

    for (int t = 0; t < ntris && count < max_out; ++t) {
        CrVertex tri[3];
        for (int k = 0; k < 3; ++k) {
            int vi;
            if (idx != NULL) {
                vi = (int)idx[t * 3 + k];
            } else {
                vi = t * 3 + k;
            }
            if (vi < 0 || vi >= nverts)
                return count; /* malformed input; stop safely */
            tri[k] = verts[vi];
        }

        CrScreenTri pair[2];
        int n = cr_transform_tri(mvp, cam->pos, tri, fb_w, fb_h, pair);
        for (int i = 0; i < n && count < max_out; ++i) {
            if (cr_screen_backface(&pair[i]))
                continue;
            out[count++] = pair[i];
        }
    }

    return count;
}
