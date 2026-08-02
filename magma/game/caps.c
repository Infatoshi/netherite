/* game/caps.c - effective-caps singleton + derived pool geometry (see game/caps.h).
 *
 * The SCALARS no longer live here: all eleven of them are registry keys
 * (core/config.def), so magma.conf is parsed by exactly one parser
 * (core/config.c) instead of two. What is left in this file is what only caps
 * knows: the derived toroidal pool geometry (mesh_D / light_D / owr_D and their
 * slot counts) and the public CrCaps view onto it.
 *
 * Zero heap: the CrCaps is a file-static struct rebuilt from cr_cfg() whenever
 * the config generation moves, so a cr_cfg_set() after the caps were first read
 * cannot leave stale geometry behind. It is all still resolved before the first
 * pool is allocated.
 */
#include "game/caps.h"
#include "core/config.h"

#include <stdio.h>
#include <stdlib.h>

static CrCaps   g_caps;
static int      g_built;
static unsigned g_built_gen;   /* cr_cfg generation g_caps was built from */

/* D = 2R + pad; slots = D*D. */
static void compute_derived(CrCaps *c) {
    int R = c->view_radius;
    if (R < 1) R = 1;
    c->view_radius = R;
    c->mesh_D  = 2 * R + 1;  c->mesh_slots  = c->mesh_D  * c->mesh_D;
    c->light_D = 2 * R + 3;  c->light_slots = c->light_D * c->light_D;
    c->owr_D   = 2 * R + 4;
    if (c->owr_D < c->owr_D_min) c->owr_D = c->owr_D_min;
    c->owr_slots = c->owr_D * c->owr_D;
}

static void caps_build(void) {
    const CrConfig *k = cr_cfg();
    g_caps.view_radius         = k->view_radius;
    g_caps.max_verts_per_chunk = k->max_verts_per_chunk;
    g_caps.owr_cells_max       = k->owr_cells_max;
    g_caps.draw_max[0]         = k->draw_solid;
    g_caps.draw_max[1]         = k->draw_cutmip;
    g_caps.draw_max[2]         = k->draw_cutout;
    g_caps.draw_max[3]         = k->draw_trans;
    g_caps.max_tris            = k->max_tris;
    g_caps.max_mobs            = k->max_mobs;
    g_caps.ent_max_verts       = k->ent_max_verts;
    g_caps.owr_D_min           = k->owr_d_min;
    compute_derived(&g_caps);
    g_built_gen = cr_cfg_generation();
    g_built     = 1;
}

void cr_caps_load(const char *path) {
    cr_cfg_load(path);
    caps_build();
}

void cr_caps_override(const char *key, long value) {
    char val[32];
    snprintf(val, sizeof val, "%ld", value);
    int rc = cr_cfg_set(key, val);
    if (rc != 0) {
        fprintf(stderr, "config: cr_caps_override(\"%s\", %ld): %s\n", key, value,
                rc == -1 ? "unknown key" : "bad value");
        exit(2);
    }
    caps_build();
}

const CrCaps *cr_caps(void) {
    /* Rebuild on ANY config movement, not just on cr_caps_load: --set and
     * cr_cfg_load can both run after some other module already took a CrCaps
     * pointer, and the geometry must follow. */
    if (!g_built || g_built_gen != cr_cfg_generation()) caps_build();
    return &g_caps;
}
