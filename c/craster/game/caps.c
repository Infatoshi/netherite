/* game/caps.c - effective-caps singleton + craster.conf loader (see game/caps.h).
 *
 * Zero heap use: the CrCaps is a file-static struct. Loading only parses a small
 * key=value text file and fills scalars, then computes the derived toroidal pool
 * geometry. All of it runs before the first pool is allocated. */
#include "game/caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CrCaps g_caps;
static int    g_loaded;

static void set_defaults(CrCaps *c) {
    c->view_radius         = CR_DEF_VIEW_RADIUS;
    c->max_verts_per_chunk = CR_DEF_MAX_VERTS_PER_CHUNK;
    c->owr_cells_max       = CR_DEF_OWR_CELLS_MAX;
    c->draw_max[0]         = CR_DEF_DRAW_SOLID;
    c->draw_max[1]         = CR_DEF_DRAW_CUTMIP;
    c->draw_max[2]         = CR_DEF_DRAW_CUTOUT;
    c->draw_max[3]         = CR_DEF_DRAW_TRANS;
    c->max_tris            = CR_DEF_MAX_TRIS;
    c->max_mobs            = CR_DEF_MAX_MOBS;
    c->ent_max_verts       = CR_DEF_ENT_MAX_VERTS;
    c->owr_D_min           = 0;
}

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

/* one "key = value" assignment; whitespace-tolerant, '#' comments handled by caller. */
static void apply_kv(CrCaps *c, const char *key, long v) {
    if      (!strcmp(key, "view_radius"))         c->view_radius         = (int)v;
    else if (!strcmp(key, "max_verts_per_chunk")) c->max_verts_per_chunk = (int)v;
    else if (!strcmp(key, "owr_cells_max"))       c->owr_cells_max       = (int)v;
    else if (!strcmp(key, "draw_solid"))          c->draw_max[0]         = (int)v;
    else if (!strcmp(key, "draw_cutmip"))         c->draw_max[1]         = (int)v;
    else if (!strcmp(key, "draw_cutout"))         c->draw_max[2]         = (int)v;
    else if (!strcmp(key, "draw_trans"))          c->draw_max[3]         = (int)v;
    else if (!strcmp(key, "max_tris"))            c->max_tris            = (int)v;
    else if (!strcmp(key, "max_mobs"))            c->max_mobs            = (int)v;
    else if (!strcmp(key, "ent_max_verts"))       c->ent_max_verts       = (int)v;
    else if (!strcmp(key, "owr_d_min"))           c->owr_D_min           = (int)v;
    /* unknown keys are ignored (forward-compatible config) */
}

void cr_caps_load(const char *path) {
    set_defaults(&g_caps);
    const char *p = path ? path : "craster.conf";
    FILE *f = fopen(p, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *hash = strchr(line, '#'); if (hash) *hash = '\0';
            char key[64]; long val;
            /* "key = value" or "key value"; %63s stops at '=' only if surrounded by
             * spaces, so also accept "key=value". Normalize '=' to space first. */
            for (char *q = line; *q; ++q) if (*q == '=') *q = ' ';
            if (sscanf(line, "%63s %ld", key, &val) == 2) apply_kv(&g_caps, key, val);
        }
        fclose(f);
    }
    compute_derived(&g_caps);
    g_loaded = 1;
}

void cr_caps_override(const char *key, long value) {
    if (!g_loaded) cr_caps_load(NULL);
    apply_kv(&g_caps, key, value);
    compute_derived(&g_caps);
}

const CrCaps *cr_caps(void) {
    if (!g_loaded) cr_caps_load(NULL);
    return &g_caps;
}
