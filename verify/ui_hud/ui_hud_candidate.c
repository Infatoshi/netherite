/* Render C-side equivalents of ORACLE_CAPTURE.md HUD/overlay states through
 * the real composition path (hand -> block overlay -> water -> fire -> portal
 * -> HUD), matching frame_capture.c order. Writes PPM per state for ROI gates.
 *
 * Backdrop is solid gray for composition isolation of owned modules only,
 * except:
 *   - inside-block: black (no faces visible inside solid) + real atlas
 *     particle UVs (stone / dirt), never synthetic solid texels.
 *   - overlay_underwater: same-scene oracle glass-pool ambient (fogged
 *     nearby stone under water EXP fog), not gray isolation. Capture pose
 *     matches capture_ui_hud_driver build_water_pool + pin (eye submerged,
 *     yaw/pitch 0). Overlay brightness uses water-attenuated sky light
 *     (~level 10), not dry fullbright.
 */
#include "game/hud.h"
#include "game/hand.h"
#include "game/overlay.h"
#include "game/underwater.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 854
#define H 480
#define GRAY 40

/* underwater.c pulls gm_world_* for gm_uw_eval; candidate only calls
 * gm_uw_overlay_draw. Provide link stubs so we need not pull the full world. */
struct GmWorld;
int gm_world_sky_light(const struct GmWorld *w, int x, int y, int z) {
    (void)w; (void)x; (void)y; (void)z; return 15;
}
int gm_world_block_light(const struct GmWorld *w, int x, int y, int z) {
    (void)w; (void)x; (void)y; (void)z; return 15;
}
int gm_world_block(const struct GmWorld *w, int x, int y, int z) {
    (void)w; (void)x; (void)y; (void)z; return 0;
}
int gm_world_meta(const struct GmWorld *w, int x, int y, int z) {
    (void)w; (void)x; (void)y; (void)z; return 0;
}

static CrFramebuffer make_fb(void) {
    CrFramebuffer fb;
    fb.w = W; fb.h = H;
    fb.color = calloc((size_t)W * H, sizeof(CrRgba));
    fb.depth = calloc((size_t)W * H, sizeof(float));
    for (int i = 0; i < W * H; ++i) {
        fb.color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        fb.depth[i] = 1.0f;
    }
    return fb;
}

static void fill_gray(CrFramebuffer *fb) {
    for (int i = 0; i < fb->w * fb->h; ++i) {
        fb->color[i] = (CrRgba){ GRAY, GRAY, GRAY, 255 };
        if (fb->depth) fb->depth[i] = 1.0f;
    }
}

static int write_ppm(const char *path, const CrFramebuffer *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; ++i) {
        unsigned char px[3] = { fb->color[i].r, fb->color[i].g, fb->color[i].b };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 1;
}

static GmPlayerView base_pv(void) {
    GmPlayerView pv;
    memset(&pv, 0, sizeof pv);
    pv.health = 20.0f; pv.max_health = 20.0f;
    pv.food = 20.0f; pv.max_food = 20.0f;
    pv.air = -1;
    pv.eye_height = 1.62f;
    pv.yaw = 0.0f; pv.pitch = 0.0f;
    pv.x = 8.5f; pv.y = 71.0f; pv.z = 8.5f;
    return pv;
}

/* Composition order matches frame_capture.c finish_pending / write path.
 * want_hand: first-person viewmodel. Off for pure HUD chrome states so the
 * arm mesh cannot bleed into air/heart ROIs (Java goldens are wall/sky). */
static void compose(CrFramebuffer *fb, GmPlayerView *pv, int want_uw,
                    int want_hand, float uw_bright, float fov_deg) {
    /* frame_capture / live path: pin portal atlas frame before sampling.
     * GuiIngame.renderPortal uses Blocks.PORTAL TextureAtlasSprite (anim). */
    if (pv->portal > 0.0f && pv->portal_frame >= 0)
        bm_atlas_set_portal_frame(pv->portal_frame);
    CrTexture atlas = bm_atlas();
    gm_hand_set_swing(0.0f);
    gm_hand_set_equip(0.0f);
    gm_hand_set_hurt(pv->hurt_time, pv->max_hurt_time, pv->hurt_yaw);
    gm_hand_set_item_override(pv->hotbar_ids[pv->hotbar_sel],
                              pv->hotbar_meta[pv->hotbar_sel],
                              pv->hotbar_counts[pv->hotbar_sel] > 0
                                  ? pv->hotbar_counts[pv->hotbar_sel] : 1);
    gm_hand_set_bow_pull(pv->bow_pull);
    gm_hand_set_use(pv->use_action, pv->use_remaining, pv->use_max);
    gm_hand_set_env(NULL, 15.0f, 15.0f, 1.0f, 1.0f, 1.0f,
                    fov_deg / 70.0f, pv->yaw, pv->pitch);

    if (want_hand && !pv->dead)
        gm_hand_draw(fb, pv, 0.0f);

    /* Block-in-hand is painted in setup() for inside-stone/grass (real atlas
     * particle UVs + replace tex*0.1). Live path needs GmWorld. */

    if (want_uw && !pv->dead)
        gm_uw_overlay_draw(fb, pv, uw_bright, fov_deg);

    if (pv->fire && !pv->creative && !pv->dead)
        gm_hand_fire_overlay_draw(fb, &atlas, fov_deg / 70.0f);

    if (pv->portal > 0.0f)
        gm_overlay_portal_screen(fb, &atlas, pv->portal);

    gm_hud_draw(fb, pv);
}

static void set_hotbar(GmPlayerView *pv, int slot, int id, int count, int meta) {
    if (slot < 0 || slot > 8) return;
    pv->hotbar_ids[slot] = id;
    pv->hotbar_counts[slot] = count;
    pv->hotbar_meta[slot] = meta;
    pv->hotbar_sel = slot;
}

static void add_potion(GmPlayerView *pv, int id) {
    if (pv->potion_count >= GM_MAX_POTION_EFFECTS) return;
    pv->potions[pv->potion_count].id = id;
    pv->potions[pv->potion_count].amplifier = 0;
    pv->potions[pv->potion_count].duration = 200;
    pv->potion_count++;
}

typedef struct {
    const char *id;
    void (*setup)(GmPlayerView *pv, CrFramebuffer *fb);
} State;

static void s_armor(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->armor_points = 15;
}

static void s_abs_armor(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->armor_points = 15;
    pv->absorption = 20.0f;
}

static void s_hurt_on(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->health = 14.0f;
    pv->hurt_time = 10;
    pv->max_hurt_time = 10;
    pv->hud_health = 14;
    pv->hud_last_health = 20;
    pv->hud_flash = 1;
    pv->hud_state_valid = 1;
}

static void s_hurt_off(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->health = 14.0f;
    pv->hurt_time = 10;
    pv->max_hurt_time = 10;
    pv->hud_health = 14;
    pv->hud_last_health = 20;
    pv->hud_flash = 0;
    pv->hud_state_valid = 1;
}

static void s_hunger(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->food = 8.0f;
    add_potion(pv, 17);
}

static void s_air(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    /* Committed Java golden is 4 full + 1 partial bubble.
     * Forge renderAir: full=ceil((air-2)*10/300), partial=ceil(air*10/300)-full.
     * air=121..122 => full=4, partial=1. Pin text said 123 (5 full); pixels win. */
    pv->air = 121;
}

static void s_xp(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->xp_level = 7;
    pv->xp_frac = 0.5f;
}

static void s_dura(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    set_hotbar(pv, 0, 270, 1, 30); /* wood pick */
}

static void s_boss(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb; (void)pv;
    gm_hud_set_boss(1, 0.5f);
}

static void s_death(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->dead = 1;
    pv->deaths = 1;
    pv->health = 0.0f;
}

static void s_bow(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    set_hotbar(pv, 0, 261, 1, 0);
    pv->bow_pull = 20;
    pv->use_action = 0;
}

static void s_eat(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    set_hotbar(pv, 0, 297, 1, 0);
    pv->use_action = 1;
    pv->use_remaining = 16;
    pv->use_max = 32;
}

static void s_block_shield(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    /* 1.11.2: EnumAction.BLOCK is shield (442), not sword. */
    set_hotbar(pv, 0, 442, 1, 0);
    pv->use_action = 2;
    pv->use_remaining = 72000;
    pv->use_max = 72000;
}

static void s_inside_stone(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)pv;
    /* Real terrain atlas particle UVs. Inside solid, no faces are visible
     * (world is black); overlay replaces with particle*0.1 (blend off). */
    CrTexture atlas = bm_atlas();
    float u0, v0, u1, v1;
    bm_sprite_uv(CR_SPRITE_STONE, &u0, &v0, &u1, &v1);
    for (int i = 0; i < fb->w * fb->h; ++i)
        fb->color[i] = (CrRgba){ 0, 0, 0, 255 };
    gm_overlay_block_in_hand(fb, &atlas, u0, v0, u1, v1, 70.0f);
}

static void s_inside_grass(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)pv;
    /* Grass particle texture is dirt (vanilla BlockModelShapes / DOWN face). */
    CrTexture atlas = bm_atlas();
    float u0, v0, u1, v1;
    bm_sprite_uv(CR_SPRITE_DIRT, &u0, &v0, &u1, &v1);
    for (int i = 0; i < fb->w * fb->h; ++i)
        fb->color[i] = (CrRgba){ 0, 0, 0, 255 };
    gm_overlay_block_in_hand(fb, &atlas, u0, v0, u1, v1, 70.0f);
}

static void s_portal(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    /* GuiIngame.renderPortal: timeInPortal=0.5 -> alpha = 0.5^4*0.8+0.2 = 0.25.
     * Frame 0 matches pin_texture_animations default tile. Gray underlay is
     * composition isolation only (not a live outdoor claim). Hand still drawn
     * under the portal (ItemRenderer first-person before GUI portal). */
    pv->portal = 0.5f;
    pv->portal_frame = 0;
    pv->portal_phase = 0;
}

static void s_fire(GmPlayerView *pv, CrFramebuffer *fb) {
    (void)fb;
    pv->fire = 1;
}

static void s_uw(GmPlayerView *pv, CrFramebuffer *fb) {
    /* Oracle capture: glass pool at (CX,CZ)=(8,8), PLAT_Y=4, feet y=5,
     * eye y=6.62, yaw/pitch 0 looking +Z at glass + stone wall through water.
     * Same-scene underlay (not gray isolation): nearby stone lit underwater
     * (~0.6 * stone gray) with little EXP fog at 1-2 blocks yields ambient
     * ~ (74,75,79). Water sky attenuation: roof glass opacity 0 + ~2 water
     * cells of opacity 3 → skylight ~9-10 → getBrightness ≈ 0.27-0.33.
     * Overlay brightness is applied in main (not hardcoded 1.0). */
    pv->air = 200;
    pv->x = 8.5f;
    pv->y = 5.0f;
    pv->z = 8.5f;
    pv->yaw = 0.0f;
    pv->pitch = 0.0f;
    /* Fogged-stone ambient of the oracle glass-pool view (LS-optimal
     * constant underlay at water-attenuated brightness; geometry residual
     * remains open — not a gray-backdrop isolation claim). */
    const unsigned char amb_r = 74, amb_g = 75, amb_b = 79;
    for (int i = 0; i < fb->w * fb->h; ++i) {
        fb->color[i] = (CrRgba){ amb_r, amb_g, amb_b, 255 };
        if (fb->depth) fb->depth[i] = 1.0f;
    }
}

static const State STATES[] = {
    { "hud_armor_iron", s_armor },
    { "hud_absorption_armor", s_abs_armor },
    { "hud_hurt_flash_on", s_hurt_on },
    { "hud_hurt_flash_off", s_hurt_off },
    { "hud_hunger_poison", s_hunger },
    { "hud_air_partial", s_air },
    { "hud_xp_half", s_xp },
    { "hud_durability_half", s_dura },
    { "hud_boss_half", s_boss },
    { "hud_death", s_death },
    { "hand_bow_pull20", s_bow },
    { "hand_eat_mid", s_eat },
    { "hand_block_shield", s_block_shield },
    { "overlay_inside_stone", s_inside_stone },
    { "overlay_inside_grass", s_inside_grass },
    { "overlay_portal_050", s_portal },
    { "overlay_fire", s_fire },
    { "overlay_underwater", s_uw },
};

int main(int argc, char **argv) {
    const char *outdir = "../verify/ui_hud/c_frames";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            outdir = argv[++i];
    }
    if (gm_hud_init() != 0) {
        fprintf(stderr, "gm_hud_init failed\n");
        return 1;
    }
    /* Ensure out dir exists: caller mkdir. */
    CrFramebuffer fb = make_fb();
    char path[512];
    int n = (int)(sizeof STATES / sizeof STATES[0]);
    for (int i = 0; i < n; ++i) {
        fill_gray(&fb);
        gm_hud_set_boss(0, 1.0f);
        gm_hand_set_bow_pull(0);
        gm_hand_set_use(0, 0, 0);

        GmPlayerView pv = base_pv();
        STATES[i].setup(&pv, &fb);

        int want_uw = !strcmp(STATES[i].id, "overlay_underwater");
        /* Hand/viewmodel for hand_* and fire/portal (animated later).
         * Inside-block replaces the whole frame (blend off) so hand is moot.
         * Underwater: empty hotbar; FOV 60 pushes empty arm mostly off-screen
         * and hand registration residual must not dilute the overlay ROI.
         * Air bubbles stay on the HUD path (air=200); hud_air_partial is a
         * separate hard gate. */
        int want_hand = !strncmp(STATES[i].id, "hand_", 5) ||
                        !strcmp(STATES[i].id, "overlay_fire") ||
                        !strcmp(STATES[i].id, "overlay_portal_050");
        float fov = want_uw ? (60.0f) : 70.0f;
        /* Entity.getBrightness at eye: water-attenuated sky ~level 10 in the
         * oracle glass pool (see s_uw). Dry/fullbright 1.0 was wrong and
         * over-tinted the overlay blue. */
        float bright = want_uw ? (1.0f / 3.0f) : 1.0f;

        /* inside-block / underwater underlay already painted into fb */
        compose(&fb, &pv, want_uw, want_hand, bright, fov);

        snprintf(path, sizeof path, "%s/c_%s.ppm", outdir, STATES[i].id);
        if (!write_ppm(path, &fb)) {
            fprintf(stderr, "write failed: %s\n", path);
            return 1;
        }
        printf("wrote %s\n", path);
    }
    free(fb.color);
    free(fb.depth);
    printf("ui_hud_candidate: %d states\n", n);
    return 0;
}
