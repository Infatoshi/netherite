/* magma demo scene contract. Shared by demo/scene.c (impl) and demo/demo_cube.c
 * (consumer). Do not change signatures without updating SPEC.md. */
#ifndef MAGMA_SCENE_H
#define MAGMA_SCENE_H
#include "core/types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Fill `verts` with a unit cube (side 1, centered at origin) as 12 triangles = 36
 * vertices (CCW front faces when viewed from outside). Each face's uv covers one
 * atlas tile; light=1, ao=1, tint = opaque white. Returns 36, or 0 if max < 36. */
int scene_cube(CrVertex *verts, int max);

/* Procedural atlas: >=16x16 RGBA, tile=16, deterministic (e.g. a colored checker or
 * dirt/grass tiles). texels point to static storage owned by scene.c; do NOT free. */
CrTexture scene_atlas(void);

#ifdef __cplusplus
}
#endif
#endif
