#define _GNU_SOURCE
/* The playable client: the native 1.7.10 server and client player ticking at
 * 20 Hz from a whole-server snapshot, the keyboard and mouse as each tick's
 * input, and the C renderer drawing every frame into an SDL window.
 *
 * Every tick's input is written as a tape row in the oracle's own form (the
 * snapshot tape's header and join rows, then one {"t", "act"} row per tick)
 * to out/native/tapes/<utc stamp>.jsonl,
 * so the session replays on the Java client:
 *   make -C oracle replay REF=<tape>        (the header marks it inputs_only)
 * and the Java replay's rows check against this same tick loop:
 *   csrc/play/check.sh <tape>
 *
 * The load and the tick pair are session.c's, the ones test_snapshots.c runs:
 * the client player's tick, the whole server's tick, then the server player's
 * (make test runs play --check on every recording). The frame is
 * renderstate_compute's numbers over the server world's chunks (its light is
 * the one the server keeps current), drawn by raster_live_frame.
 *
 *   out/native/play [SNAPSHOT_DIR] [--assets SCENE_DIR] [--tape OUT.jsonl] [--threads N]
 *   out/native/play SNAPSHOT_DIR --prefix-tape REF --prefix-until T --input-script EVENTS.jsonl
 *   out/native/play[-dev] SNAPSHOT_DIR --check REF    (headless: REF's rows through this tick pair)
 *   out/native/play SNAPSHOT_DIR --replay INPUTS --tape OUT   (headless: INPUTS' t and act rows
 *                                  played, OUT this client's tape; csrc/play/chain.sh)
 *
 * The observation's own shape, on any of them: --size WxH (854x480 by
 * default; the GUI scale is ScaledResolution's at that size), --hide-gui
 * (F1 from the start) and --render-distance N (the renderer's, not the
 * tape's; csrc/tests/frame_judge.sh's OBS file sets them); --obs is the
 * RL observation's (raster_obs.h: 128x128, F1, render distance 4). With --shots,
 * --obs-dump DIR writes each shot's terrain passes and their C frame for the
 * device renderer's gate (csrc/cuda/render; the goldens are optional);
 * --obs-mesh 2 adds the device mesher's feed (engine/meshfeed.h) and the host's
 * meshes of it (cuda/meshing's gate). --render-prec exact|fast|fast:STAGE,...
 * draws at that precision (engine/raster_prec.h; exact, the default, is the
 * one judged against Java).
 * The shots' world frames are drawn on the device when out/native/cuda/
 * libframedev.so is there (csrc/play/framedev.h; Linux, the dev host's 3090),
 * every 16th again by the C renderer to be compared byte for byte;
 * --frames c draws them all in C, --frames device fails without the
 * device, --frames-check N compares every Nth (1: all of them).
 *
 * Keys: WASD move, space jump, shift sneak, left ctrl sprint, mouse attack and
 * use, E opens or closes inventory, 1-9 and the wheel pick the hotbar slot,
 * Q drops, the Respawn button respawns after a death (GuiGameOver takes no
 * key: R does nothing, as in vanilla), Esc pauses or closes a
 * container, Cmd-Q or closing the window ends the session. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* The Makefile's -include engine/envheap.h makes malloc an object-like macro;
 * the Mac's SDL headers spell __attribute__((malloc)), which the rename turns
 * into an unknown attribute (an error under -Werror). SDL loads without it. */
#pragma push_macro("malloc")
#undef malloc
#include <dirent.h>
#include <stdarg.h>
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#endif
#pragma pop_macro("malloc")

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "../engine/cwrand.h"
#include "../engine/chat.h"
#include "../engine/chatcomp.h"
#include "../engine/font_cw.h"
#include "../engine/env.h"
#include "../engine/envmem.h"
#include "../engine/chunkload.h"
#include "../engine/dev.h"
#include "../engine/container.h"
#include "../engine/grave.h"
#include "../engine/gui_input.h"
#include "../engine/gui_screens.h"
#include "../engine/nbtjson.h"
#include "../engine/pngread.h"
#include "../engine/player.h"
#include "../engine/raster.h"
#include "../engine/raster_prec.h"
#include "../engine/raster_entities.h"
#include "../engine/raster_mobs.h"
#include "../engine/raster_particles.h"
#include "../engine/raster_worldfx.h"
#include "../engine/survival.h"
#include "../engine/raster_hand.h"
#include "../engine/firework.h"
#include "../engine/raster_tileent.h"
#include "../engine/raster_things.h"
#include "../engine/raytrace.h"
#include "../engine/fallhang.h"
#include "../engine/tileentity.h"
#include "../engine/render_blocks.h"
#include "../engine/renderstate.h"
#include "../engine/raytrace.h"
#include "../engine/clientstate.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/combat.h"
#include "../engine/snapshot.h"
#include "../engine/survival.h"
#include "../engine/tape.h"
#include "../engine/ticks.h"
#include "../engine/world.h"
#include "../engine/image.h"
#include "../engine/endfight.h"
#include "../engine/jmath.h"
#include "../engine/texanim.h"
#include "../engine/pickobj.h"
#include "../engine/dragon.h"
#include "../engine/smath.h"
#include "../engine/ai.h"
#include "../engine/blocks.h"
#include "../engine/collide.h"
#include "../engine/item_entity.h"
#include "../engine/raster_itemframe.h"
#include "../engine/leash.h"
#include "../engine/raster_obs.h"
#include "playview.h"
#include "framedev.h"

_Static_assert(RB_CHUNK_BYTES == SNAP_CHUNK_BYTES, "the render world's chunk record is the snapshot's layout");

/* The frame's size (--size WxH; vanilla's window, 854x480 by default) and
 * the window's pixel doubling. */
static int W = 854, H = 480;
enum { SCALE = 2 };

/* ---------------------------------------------------------------- session */

/* The load, the start and the tick pair are session.c's, the same ones the
 * replay gate (test_snapshots.c) runs; the client adds its frames. */
#ifdef PLAY_VIEW
/* the library build (csrc/runtime/view.c, playview.h): the session and
 * its snapshot are a pool environment's, pv_open's */
static struct session *pv_ss;
#define SS (*pv_ss)
#define S (*pv_ss->s)
#else
static struct snapshot S;
static struct session SS;
#endif
#define CP (SS.cp)
#define SP (SS.sp)
#define SR (*SS.sr)
#define CW (SS.client_world)
/* GuiScreen's input queue for the container screens (gui_input.h): the
 * events arrive here between ticks and the tick's gui phase runs them */
static struct gui_input gin;
static int gui_mouse_x, gui_mouse_y;
/* the dimension the render world holds: the player's, rebuilt when an S07
 * moves the client into another one */
static int view_dim;

/* WorldClient.skylightSubtracted, which the client's getLightBrightness
 * (the fog's brightness, Entity.getBrightness for the vignette and the
 * overlays) subtracts: only World's constructor sets it on the client
 * (calculateInitialSkylight; WorldServer.tick alone keeps it current), at
 * a new WorldInfo's time 0 and no rain, so it stays 0 under an overworld or
 * End sky, whatever the time or the weather, and 11 in the Nether (its
 * celestial angle is 0.5), where no block has sky light. */
static int client_skylight_subtracted(void)
{
    return view_dim == -1 ? 11 : 0;
}
static const char *view_assets;
static const char *view_assets;
static int view_threads;

static struct world *view_world(void)
{
    return view_dim == -1 ? &SR.hell.world : view_dim == 1 ? &SR.sky.world : &SR.pop.world;
}

static const struct servertick *view_st(void)
{
    for (int i = 0; i < SR.nworlds; ++i)
        if (SR.w[i].dim == view_dim) return &SR.w[i].st;
    return &SR.w[0].st;
}

/* WorldClient.weatherEffects and lastLightningBolt: each bolt the server's
 * last tick spawned (its S2C) is built on the client at the next tick's
 * packets (clientstate.c), from the client's own streams */
static struct cs_weather live_weather;
static int live_bolts_pending, live_bolt_ids;
/* where each bolt of live_weather stands (the S2C's position), by id */
static struct { int id; double x, y, z; } live_bolt_pos[CS_BOLTS];
static double pending_bolt_pos[CS_BOLTS][3];
static float live_pt = 1.0F;   /* the partial tick of the frame being drawn */

/* Det's client consumer pin (det.h): the torch flicker's and the spawned
 * items' own streams, and the particles' (particles_live_pin), at client
 * tick live_t of the world seed live_seed. */
static int64_t live_t, live_seed;
static det_pin pin_flicker, pin_item;

#include "clientents.c"

static void chest_lids_after_row(void);
static void chest_lids_reset(void);
static void tileents_tick(void);

/* NetHandlerPlayClient.handleRespawn's new WorldClient has no weather yet */
static void live_world_changed(void *ctx, int dim)
{
    (void)ctx;
    (void)dim;
    cents_world_changed();
    chest_lids_reset();
    memset(&live_weather, 0, sizeof live_weather);
    live_bolts_pending = 0;
}

/* the client world's bolts, with the packets and before the player's tick */
static void live_before_client_tick(void *ctx, int paused)
{
    (void)ctx;
    cents_before_client_tick(paused);
    if (paused) return;
    for (int k = 0; live_bolts_pending > 0; --live_bolts_pending, ++k)
    {
        clientstate_bolt_spawn(&live_weather, ++live_bolt_ids, &SR_DET(&SR));
        int q = live_bolt_ids % CS_BOLTS;
        live_bolt_pos[q].id = live_bolt_ids;
        if (k < CS_BOLTS)
        {
            live_bolt_pos[q].x = pending_bolt_pos[k][0];
            live_bolt_pos[q].y = pending_bolt_pos[k][1];
            live_bolt_pos[q].z = pending_bolt_pos[k][2];
        }
    }
    clientstate_weather_tick(&live_weather);
}

/* the bolts this server tick spawned: ticked once already */
static void live_after_world_tick(void *ctx)
{
    (void)ctx;
    cents_after_world_tick();
    live_bolts_pending = 0;
    for (int i = 0; i < SR.d->nbolts; ++i)
        if (SR.d->bolts[i].ticks_existed == 1 && !SR.d->bolts[i].dead)
        {
            if (live_bolts_pending < CS_BOLTS)
            {
                pending_bolt_pos[live_bolts_pending][0] = SR.d->bolts[i].x;
                pending_bolt_pos[live_bolts_pending][1] = SR.d->bolts[i].y;
                pending_bolt_pos[live_bolts_pending][2] = SR.d->bolts[i].z;
            }
            ++live_bolts_pending;
        }
}
static int fancy_assets = 1;

static int recorded_fancy(const char *assets)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", assets);
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) { free(last); last = strdup(line); }
    free(line);
    fclose(f);
    if (!last) return 1;
    struct jval *root = json_parse(last);
    if (!root) { free(last); return 1; }
    const struct jval *v = json_get(json_get(root, "opt"), "fancy");
    if (!v) v = json_get(json_get(root, "hud"), "fancy");
    int64_t n = 1;
    if (v) json_int(v, &n);
    json_free(root);
    return n != 0;
}

static struct tape source_tape;
static const struct jval *source_setups;
#ifdef NETHERITE_DEV
static struct jval *sidecar[512];
static int nsidecar;
#endif

#ifdef NETHERITE_DEV
static int setup_tick(const struct jval *entry)
{
    int64_t tick = -1;
    return json_int(json_get(entry, "tick"), &tick) ? (int)tick : -1;
}

static int load_sidecar(const char *path)
{
    if (!path) return 1;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "play-dev: cannot read %s\n", path); return 0; }
    char *line = NULL;
    size_t cap = 0;
    int last = -1;
    while (getline(&line, &cap, f) > 0)
    {
        if (nsidecar >= 512) { fprintf(stderr, "play-dev: too many sidecar entries\n"); fclose(f); free(line); return 0; }
        struct jval *entry = json_parse(strdup(line));
        const char *cls = json_str(json_get(entry, "class"));
        int tick = setup_tick(entry);
        const struct jval *cmd = json_get(entry, "cmd");
        if (!entry || !cls || strcmp(cls, "Dev") || tick < (int)S.tick || tick < last ||
            !cmd || cmd->kind != J_OBJ)
        {
            fprintf(stderr, "play-dev: sidecar needs ordered Dev setup entries at or after snapshot tick\n");
            json_free(entry);
            fclose(f);
            free(line);
            return 0;
        }
        sidecar[nsidecar++] = entry;
        last = tick;
    }
    free(line);
    fclose(f);
    return 1;
}

static void write_merged_setup(FILE *out)
{
    int i = 0, j = 0, first = 1;
    while (i < json_len(source_setups) || j < nsidecar)
    {
        const struct jval *source = i < json_len(source_setups) ? json_at(source_setups, i) : NULL;
        const struct jval *side = j < nsidecar ? sidecar[j] : NULL;
        const struct jval *entry = !side || (source && setup_tick(source) <= setup_tick(side)) ? source : side;
        if (entry == source) ++i; else ++j;
        char *raw = json_raw(entry);
        fprintf(out, "%s%s", first ? "" : ",", raw);
        free(raw);
        first = 0;
    }
}
#endif
/* GL_NORMALIZE: RenderSlime.shouldRenderPass turns it on for a slime's
 * outer layer and nothing in the client turns it off, so from the first
 * slime drawn every later frame normalizes the normals a scaled modelview
 * lengthens (the sign board's, drawn without GL_RESCALE_NORMAL) */
static int gl_normalize;
static int hud_update_counter, hud_scale = 2, hud_fancy = 1;
static float hud_vignette = 1.0F;
static const char *hud_assets;
static const char *mob_assets;   /* the scene the mob textures come from (--assets) */
static uint32_t frame_lm[256];   /* the lightmap the last world frame drew with (a copy) */
static void live_player_render(struct mob_render_input *m, float pt);
static float lbt[16];
static int hud_last_item = -1, hud_last_meta, hud_last_tag, hud_highlight_ticks;
/* GuiIngame.highlightingItemStack: the held stack updateTick last saw, the
 * one whose name the HUD draws */
static struct surv_stack hud_highlight_stack;
/* GuiNewChat's messages: StatisticsFile.func_150873_a's achievement
 * announcement, pushed when the client's S37 unlocks the toast, and the
 * server's S02 lines (the bed's refusals, the death message) */
static struct chat live_chat;
/* Minecraft.getSystemTime as the oracle's agent frames see it: Oracle.tick
 * (one past the row the frame follows) times 50 ms, the glint's scroll; the
 * frame being drawn sets it */
static int64_t frame_now_ms;

/* RenderGlobal.damagedBlocks for the other entities: the S25s the server's
 * destroyBlockInWorldPartially sent (env.h block_break), handled at the next
 * client tick's packets (the counter as updateClouds then finds it), and the
 * entries updateClouds drops past 400 ticks */
static struct { int used, id, x, y, z, stage, tick; } dmg_map[16];
static struct { int id, x, y, z, stage; } dmg_pend[16];
static int ndmg_pend;
static int cloud_tick;

static void damaged_blocks_after_row(int paused)
{
    if (!paused)
    {
        for (int i = 0; i < ndmg_pend; ++i)
        {
            /* WorldManager.destroyBlockPartially: to the players within 32 */
            double dx = (double)dmg_pend[i].x - SP.e.pos_x, dy = (double)dmg_pend[i].y - SP.e.pos_y,
                   dz = (double)dmg_pend[i].z - SP.e.pos_z;
            if (dx * dx + dy * dy + dz * dz >= 1024.0) continue;
            int k = -1, fr = -1;
            for (int j = 0; j < 16; ++j)
            {
                if (dmg_map[j].used && dmg_map[j].id == dmg_pend[i].id) k = j;
                if (!dmg_map[j].used && fr < 0) fr = j;
            }
            if (dmg_pend[i].stage < 0 || dmg_pend[i].stage >= 10)
            {
                if (k >= 0) dmg_map[k].used = 0;
                continue;
            }
            if (k < 0 || dmg_map[k].x != dmg_pend[i].x || dmg_map[k].y != dmg_pend[i].y || dmg_map[k].z != dmg_pend[i].z)
            {
                if (k < 0) k = fr;
                if (k < 0) continue;
                dmg_map[k].used = 1;
                dmg_map[k].id = dmg_pend[i].id;
                dmg_map[k].x = dmg_pend[i].x; dmg_map[k].y = dmg_pend[i].y; dmg_map[k].z = dmg_pend[i].z;
            }
            dmg_map[k].stage = dmg_pend[i].stage;
            dmg_map[k].tick = cloud_tick - 1;   /* the tick updated the counter after the packets */
        }
        if (cloud_tick % 20 == 0)
            for (int j = 0; j < 16; ++j)
                if (dmg_map[j].used && cloud_tick - dmg_map[j].tick > 400) dmg_map[j].used = 0;
        ndmg_pend = 0;
    }
    for (int i = 0; i < nw_env->block_break.n && ndmg_pend < 16; ++i, ++ndmg_pend)
    {
        dmg_pend[ndmg_pend].id = nw_env->block_break.e[i].id;
        dmg_pend[ndmg_pend].x = nw_env->block_break.e[i].x;
        dmg_pend[ndmg_pend].y = nw_env->block_break.e[i].y;
        dmg_pend[ndmg_pend].z = nw_env->block_break.e[i].z;
        dmg_pend[ndmg_pend].stage = nw_env->block_break.e[i].stage;
    }
}

/* EntityClientPlayerMP's DataWatcher 9 (the arrows in it): the S1C the
 * server's tracker pass sent at the last row reaches the client at the next,
 * so the client holds the value of the row before */
static int client_arrows, server_arrows_sent;
/* GuiWinGame's clock (field_146581_h, one per client tick, paused or not)
 * and GuiInventory's stored mouse (the last drawScreen's, 0 when opened) */
static int credits_ticks;
static int sleep_ticks;   /* GuiTextField.cursorCounter of the GuiSleepMP's field */

/* GuiWinGame.updateScreen: the roll ends by itself once its clock passes
 * (field_146579_r + height + height + 24) / 0.5, the text's lines at 12
 * pixels and two screen heights (scaled), and func_146574_g sends the
 * PERFORM_RESPAWN and closes the screen as escape does (the row's
 * ["respawn"] op) */
static int credits_roll_ends(int clock)
{
    /* the tick's own end (the oracle's screen), so the tape's op and the
     * replay's clock agree */
    return (float)clock > surv_credits_end(&CP);
}
static float inv_mouse_x, inv_mouse_y;
static int inv_was_open;
static int death_screen_ticks;
static int play_rd = 8, play_bob = 1, play_clouds = 1, play_fancy = -1;
/* --render-distance N: the renderer's own distance, the observation's (the
 * tape's rd, which the server's view distance follows, is untouched); 0
 * draws at the tape's. --hide-gui: vanilla's F1 held from the start (no HUD
 * but under a screen, no hand, no block outline). */
static int play_view_rd, play_hide_gui;
static int render_rd(void) { return play_view_rd > 0 ? play_view_rd : play_rd; }
static int gui_hidden(void) { return CP.opt_hide || play_hide_gui; }
/* the player's own fov (0 to 1: 70 to 110 degrees), gamma, mouse sensitivity
 * and invertYMouse, from --options (load_options); the defaults are vanilla's */
static float opt_fov = 0.0F, opt_gamma = 0.0F, opt_sens = 0.5F;
static int opt_invert = 0;
static struct raster_live *RL;
static void rw_on_chunk(void *ctx, char kind, int cx, int cz);

static void draw_block_overlays(unsigned char *rgb, const struct rs_out *o)
{
    /* drawBlockDamageTexture's other entries (a zombie's door): one past 32
     * of the camera leaves the map, one on air is skipped; before the
     * player's own and the selection */
    for (int j = 0; j < 16; ++j)
    {
        if (!dmg_map[j].used) continue;
        double ex = (double)dmg_map[j].x - o->camx, ey = (double)dmg_map[j].y - o->camy,
               ez = (double)dmg_map[j].z - o->camz;
        if (ex * ex + ey * ey + ez * ez > 1024.0) { dmg_map[j].used = 0; continue; }
        if ((world_get_block(view_world(), dmg_map[j].x, dmg_map[j].y, dmg_map[j].z) & 4095) == 0) continue;
        raster_live_overlay(RL, o, rgb, 0, 0, 0, 0, NULL, dmg_map[j].stage, dmg_map[j].x, dmg_map[j].y, dmg_map[j].z);
    }
    int dx = 0, dy = 0, dz = 0;
    int stage = surv_client_dig_stage(&dx, &dy, &dz);
    /* drawSelectionBox's getSelectedBoundingBoxFromPool after
     * setBlockBoundsBasedOnState: a fence's or pane's arms, a door's
     * combined halves; the cactus and the cake answer their own box */
    double box[6];
    if (surv_mo.cur.num)
    {
        int sid = world_get_block(CW, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;
        if (sid == 81)
        {
            box[0] = box[2] = 0.0625; box[1] = 0.0;
            box[3] = box[5] = 1.0 - 0.0625; box[4] = 1.0;
        }
        else if (sid == 92)
        {
            box[0] = (double)((float)(1 + (world_get_meta(CW, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 15) * 2) / 16.0F);
            box[1] = 0.0; box[2] = 0.0625;
            box[3] = 1.0 - 0.0625; box[4] = 0.5; box[5] = 1.0 - 0.0625;
        }
        else raytrace_block_bounds(CW, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, box);
    }
    /* EntityRenderer.renderWorld draws the selection only while
     * !gameSettings.hideGUI (F1 hides it; the damage stays) */
    int sel = surv_mo.cur.num && !gui_hidden();
    raster_live_overlay(RL, o, rgb, sel,
                        surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, sel ? box : NULL,
                        stage, dx, dy, dz);
}

/* A checkpoint start whose snapshot predates stats.json (the golden
 * chain's G2 to G7): the server's StatisticsFile is the checkpoint's
 * save/stats file, and the join's S37 gave the client every achievement in
 * it. Without them every achievement toasted and chatted again on its next
 * trigger, and one whose parent the file held (killEnemy after buildSword)
 * never unlocked. The playable client only: the replay gates load no stats
 * for these snapshots and compare none. */
static void legacy_client_stats(void)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/stats", SR.save_dir);
    DIR *d = SR.save_dir[0] ? opendir(path) : NULL;
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        size_t n = strlen(de->d_name);
        if (n < 6 || strcmp(de->d_name + n - 5, ".json")) continue;
        char file[1700];
        snprintf(file, sizeof file, "%s/%s", path, de->d_name);
        FILE *f = fopen(file, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *file_text = malloc((size_t)len + 1);
        size_t got = fread(file_text, 1, (size_t)len, f);
        fclose(f);
        file_text[got] = 0;
        size_t cap = 2 * got + 64;
        char *text = malloc(cap);
        snprintf(text, cap, "{\"file\":%s,\"client\":{\"file\":%s}}", file_text, file_text);
        surv_stats_load_snapshot(&SP, &CP, text, (int)S.tick - 1);
        free(text);
        free(file_text);
        break;
    }
    closedir(d);
}

/* The snapshot and its tape, then session.c's open. A --check run reads
 * the snapshot's gzip files without their CRC-32s, as test_snapshots'
 * replay does (the recording's load check reads them with). */
static int play_open(const char *dir, int checking)
{
    if (!snapshot_load_crc(&S, dir, !checking)) { fprintf(stderr, "play: %s did not load as a snapshot\n", dir); return 0; }
    live_seed = S.seed;
    det_pin_init(&pin_flicker, DET_PIN_FLICKER, S.seed);
    det_pin_init(&pin_item, DET_PIN_ITEM, S.seed);
    char tape_path[1200];
    snprintf(tape_path, sizeof tape_path, "%s/tape.jsonl", dir);
    if (!tape_open(&source_tape, tape_path))
    {
        fprintf(stderr, "play: cannot read %s\n", tape_path);
        return 0;
    }
    source_setups = json_get(source_tape.hdr, "setup");
#ifdef NETHERITE_DEV
    int dev_build = 1;
#else
    int dev_build = 0;
#endif
    if (session_refuse_dev(source_tape.hdr, S.manifest_json, dev_build))
    {
        fprintf(stderr, "REFUSE dev entry in a non-dev tape\n");
        exit(4);
    }
    if (!session_open(&SS, &S, dir, source_tape.hdr))
    {
        fprintf(stderr, "play: %s: %s\n", dir, SS.err);
        return 0;
    }
    /* the client world's Random at the bind, from the snapshot tape's rows
     * (cwrand.h); recovered only from rows t0 - 1 .. t0 + 1, the bind's own
     * state (a live session's ticks are not the recording's) */
    {
        uint64_t cw_seed = 0;
        int32_t cw_lcg = 0;
        int64_t cw_from = -1;
        cwrand_from_tape(tape_path, (int64_t)S.tick, &cw_seed, &cw_lcg, &cw_from);
        if (cw_from == (int64_t)S.tick) client_player_set_world_rand(&SS.cp, cw_seed, cw_lcg);
    }
#ifdef NETHERITE_DEV
    SS.dev_apply = dev_apply;
    SS.dev_end = dev_end;
#endif
    SS.hooks.on_chunk = rw_on_chunk;
    SS.hooks.world_changed = live_world_changed;
    SS.hooks.before_client_tick = live_before_client_tick;
    SS.hooks.after_world_tick = live_after_world_tick;
    return 1;
}

/* EntityPickupFX, the live client's copy: the server's pickup reaches the
 * client as an S0D (the row's SP.s0d_ids), whose handler adds the effect
 * (the collected item or orb drawn by its own renderer, lit as particles
 * are, moving from where it lay toward the collector's position 0.5 under
 * its posY by ((age + pt) / 3)^2) and takes the entity out of the world,
 * even when a partial take left the server's item. The effect lives three
 * ticks. */
/* PICKUP_MAX: EffectRenderer.addEffect's 4000 a layer (the oldest goes);
 * DROPS_SEEN_MAX: both item worlds' entities */
enum { PICKUP_MAX = 4000, DROPS_SEEN_MAX = S0D_MAX, WFX_MAX = S0D_MAX + PICKUP_MAX };
static struct pickup_fx { struct worldfx_entity e; double x, y, z; int age, resting, copy; } pickups[PICKUP_MAX];
static int npickups;
/* EntityFX.interpPos as renderLitParticles reads it: renderParticles sets
 * it after the lit pass, so the pickup draws against the previous frame's
 * camera (the live loop's last frame; a lone shot takes the tick's start,
 * as a frame drawn after every tick leaves it) */
static double fx_interp[3];
static int fx_interp_set;
static struct seen_drop { int id, delay; struct worldfx_entity e; struct aabb box; } drops_seen[DROPS_SEEN_MAX];
static int ndrops_seen;

static void drop_fill(struct worldfx_entity *w, const ie_ent *it)
{
    memset(w, 0, sizeof *w);
    w->x = it->e.pos_x;
    w->y = it->e.pos_y;
    w->z = it->e.pos_z;
    int bx = (int)floor(w->x), by = (int)floor(w->y + it->e.height * 0.66), bz = (int)floor(w->z);
    w->light = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
               (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
    if (it->kind == IE_ORB)
    {
        w->orb = 1;
        w->count = it->xp_value;
        w->color = it->xp_color;
        int low = (w->light & 255) + 120;
        if (low > 240) low = 240;
        w->light = low | (w->light & ~255);
        return;
    }
    w->id = it->stack_item;
    w->meta = it->stack_damage;
    w->count = it->stack_count;
    w->tag = it->stack_tag;
    w->age = it->age;
    w->hover = it->hover_start;
}

static const ie_world *drop_lists(int l)
{
    if (!SS.server_rows) return NULL;
    return l == 0 ? &SR.d->iew : SR.mobs_enabled ? &SR.d->anw.iew : NULL;
}

static void drops_before_tick(void)
{
    ndrops_seen = 0;
    for (int l = 0; l < 2; ++l)
    {
        const ie_world *iw = drop_lists(l);
        for (int i = 0; iw && i < iw->n && ndrops_seen < DROPS_SEEN_MAX; ++i)
        {
            const ie_ent *it = ie_ent_at(iw->slot[i]);
            if (!it || it->is_dead || (it->kind != IE_ITEM && it->kind != IE_ORB)) continue;
            struct seen_drop *d = &drops_seen[ndrops_seen++];
            d->id = it->entity_id;
            d->delay = it->delay;
            d->box = aabb_expand(it->e.bounding_box, fabs(it->e.motion_x) + 0.1, fabs(it->e.motion_y) + 0.1,
                                 fabs(it->e.motion_z) + 0.1);
            drop_fill(&d->e, it);
        }
    }
}

static void drops_after_tick(void)
{
    /* the client meets the pickup a tick after the server (the S0D): until
     * then the drop still lies where it was; then EffectRenderer.updateEffects
     * ages the effect each tick, the third ends it */
    int k = 0;
    for (int i = 0; i < npickups; ++i)
    {
        if (pickups[i].resting) { pickups[i].resting = 0; pickups[i].e.unlit = 1; }
        if (++pickups[i].age < 3) pickups[k++] = pickups[i];
    }
    npickups = k;
    if (!SS.server_rows) return;
    for (int s = 0; s < SP.ns0d; ++s)
    {
        const struct seen_drop *d = NULL;
        for (int i = 0; i < ndrops_seen && !d; ++i)
            if (drops_seen[i].id == SP.s0d_ids[s]) d = &drops_seen[i];
        /* one that joined this row (an orb a death dropped on the player):
         * its spawn reached the client just ahead of the S0D */
        struct seen_drop fresh;
        for (int l = 0; l < 2 && !d; ++l)
        {
            const ie_world *iw = drop_lists(l);
            for (int j = 0; iw && j < iw->n && !d; ++j)
            {
                const ie_ent *it = ie_ent_at(iw->slot[j]);
                if (!it || it->entity_id != SP.s0d_ids[s] || (it->kind != IE_ITEM && it->kind != IE_ORB)) continue;
                fresh.id = it->entity_id;
                fresh.delay = it->delay;
                fresh.box = it->e.bounding_box;
                drop_fill(&fresh.e, it);
                /* the copy the spawn packet made at the tracker's first
                 * contact (spawnEntityInWorld: the position before the
                 * entity's first update): the S0E's 1/32 block position, and
                 * the S11's raw fields for an orb, which handleSpawnExperience
                 * Orb does not divide */
                double sx = it->ticks_existed > 0 ? it->prev_x : it->e.pos_x;
                double sy = it->ticks_existed > 0 ? it->prev_y : it->e.pos_y;
                double sz = it->ticks_existed > 0 ? it->prev_z : it->e.pos_z;
                double div = it->kind == IE_ORB ? 1.0 : 32.0;
                fresh.e.x = (double)mh_floor(sx * 32.0) / div;
                fresh.e.y = (double)mh_floor(sy * 32.0) / div;
                fresh.e.z = (double)mh_floor(sz * 32.0) / div;
                d = &fresh;
            }
        }
        if (d == NULL) continue;
        if (npickups >= PICKUP_MAX)
        {
            memmove(&pickups[0], &pickups[1], (size_t)(PICKUP_MAX - 1) * sizeof pickups[0]);
            --npickups;
        }
        struct pickup_fx *f = &pickups[npickups++];
        f->e = d->e;
        f->x = d->e.x;
        f->y = d->e.y;
        f->z = d->e.z;
        f->age = 0;
        f->resting = 1;
        f->copy = 0;
        /* the client's own copy is what the S0D takes: it lies where it is
         * until the next client tick, then the effect starts from it */
        struct citem *cc = citem_find(d->id);
        if (cc && cc->has_copy)
        {
            /* the integrated server passes packets as objects: the copy's
             * DataWatcher holds the server item's own ItemStack, which
             * addItemStackToInventory just emptied in place */
            if (cc->kind == IE_ITEM) cc->count = 0;
            citem_fill(&f->e, cc, 1.0F);
            f->x = cc->e.pos_x;
            f->y = cc->e.pos_y;
            f->z = cc->e.pos_z;
            f->copy = 1;
            cc->collected = 1;
        }
        /* its copy is built from the spawn at the next client tick and the
         * S0D takes it at once: nothing lay there before */
        else if (cc && d == &fresh)
        {
            f->copy = 1;
            cc->collect_on_spawn = 1;
        }
    }
}

/* GuiNewChat's scroll (field_146250_j) and its new-line flag
 * (field_146251_k), which the chat screen moves and the drawn chat reads */
static int live_chat_scroll, live_chat_scrolled;

/* the chat's line width (func_146228_f at chatWidth 1 over chatScale 1) and
 * the text field's widths as metrics */
static const struct font_metrics *chat_font(void)
{
    static struct font_metrics m;
    static int made;
    if (!made)
    {
        for (int i = 0; i < 256; ++i) m.cw[i] = FONT_CW[i];
        made = 1;
    }
    return &m;
}

static int live_chat_message_lines(const struct chat_message *m)
{
    int n = 0;
    char **l = chat_split(chat_font(), m, chat_width(1.0f), 1, &n);
    for (int i = 0; i < n; ++i) free(l[i]);
    free(l);
    return n;
}

/* field_146253_i.size(): the newest 100 lines */
static int live_chat_line_count(void)
{
    int n = 0;
    for (int i = 0; i < live_chat.nmsg && n < CHAT_MAX; ++i) n += live_chat_message_lines(&live_chat.msg[i]);
    return n < CHAT_MAX ? n : CHAT_MAX;
}

/* func_146229_b, with the chat open (func_146232_i: 20 lines) */
static void live_chat_scroll_by(int d)
{
    live_chat_scroll += d;
    int n = live_chat_line_count();
    if (live_chat_scroll > n - 20) live_chat_scroll = n - 20;
    if (live_chat_scroll <= 0)
    {
        live_chat_scroll = 0;
        live_chat_scrolled = 0;
    }
}

/* GuiNewChat.func_146236_a at the pointer (display pixels, y up), with a
 * GuiChat up: the drawn line's piece past the pointer's x, as its message
 * and part and its text; NULL none */
static const struct chat_message *live_chat_piece(int px, int py, int *part, char *text, size_t nt)
{
    const int sf = GC_DISPLAY_W / GC_SCREEN_W;
    int x = px / sf - 3, y = py / sf - 27;
    if (x < 0 || y < 0) return NULL;
    int n = live_chat_line_count();
    int shown = n < 20 ? n : 20;
    if (x > chat_width(1.0f) || y >= 9 * shown + shown) return NULL;
    int want = y / 9 + live_chat_scroll;
    if (want < 0 || want >= n) return NULL;
    /* the line, newest first: each message's lines bottom up */
    int line = 0;
    for (int i = 0; i < live_chat.nmsg; ++i)
    {
        const struct chat_message *m = &live_chat.msg[i];
        static struct chat_piece pieces[256];
        int np = 0, nl = 0;
        chat_split_pieces(chat_font(), m, chat_width(1.0f), 1, pieces, 256, &np, &nl);
        const struct chat_message *found = NULL;
        if (want < line + nl)
        {
            int ml = nl - 1 - (want - line);
            int w = 0;
            for (int k = 0; k < np && !found; ++k)
            {
                if (pieces[k].line != ml) continue;
                w += font_string_width(chat_font(), pieces[k].text);
                if (w > x)
                {
                    found = m;
                    *part = pieces[k].part;
                    snprintf(text, nt, "%s", pieces[k].text);
                }
            }
        }
        for (int k = 0; k < np; ++k) free(pieces[k].text);
        if (want < line + nl) return found;
        line += nl;
    }
    return NULL;
}

/* GuiChat.mouseClicked's component: one with a click (the SUGGEST_COMMAND
 * value and the piece's text); 0 none */
static int live_chat_comp(int px, int py, char *value, size_t nv, char *text, size_t nt)
{
    int part = 0;
    const struct chat_message *m = live_chat_piece(px, py, &part, text, nt);
    if (m == NULL || m->click[part] == NULL) return 0;
    snprintf(value, nv, "%s", m->click[part]);
    return 1;
}

/* GuiChat.drawScreen's hover: a SHOW_ACHIEVEMENT piece under the pointer
 * gives the tooltip's lines (the achievement's name in its colour,
 * stats.tooltip.type.achievement in italics, the description wrapped at
 * 150); their count */
static int live_chat_tooltip(int px, int py, char (*lines)[192], int max)
{
    int part = 0;
    char text[GC_OUT_LEN];
    const struct chat_message *m = live_chat_piece(px, py, &part, text, sizeof text);
    if (m == NULL || m->hover[part] == NULL) return 0;
    int a = surv_ach_by_id(m->hover[part]);
    if (a < 0) return 0;
    int n = 0;
    snprintf(lines[n++], 192, "\302\247%c%s\302\247r", surv_ach_special(a) ? '5' : 'a', surv_ach_name(a));
    snprintf(lines[n++], 192, "\302\247oAchievement\302\247r");
    int nd = 0;
    char **d = font_list_to_width(chat_font(), surv_ach_desc(a), 150, &nd);
    for (int i = 0; i < nd && n < max; ++i) snprintf(lines[n++], 192, "%s", d[i]);
    font_list_free(d, nd);
    return n;
}

static int chat_was_up;

/* --cw-hash: each client section's light (sky, block and id per cell, x then
 * z then y, Chunk.getSavedLightValue's) as FNV-1a 64 over the nine by nine
 * chunks around the player, the oracle's CwDiff.java hash */
static int64_t cwhash_ticks[64];
static int ncwhash;

static void cw_hash_print(int64_t t)
{
    int want = 0;
    for (int i = 0; i < ncwhash; ++i) want |= cwhash_ticks[i] == t;
    if (!want || CW == NULL) return;
    int pcx = (int)floor(CP.e.pos_x / 16.0), pcz = (int)floor(CP.e.pos_z / 16.0);
    for (int cx = pcx - 4; cx <= pcx + 4; ++cx)
        for (int cz = pcz - 4; cz <= pcz + 4; ++cz)
        {
            if (world_chunk(CW, cx, cz) == NULL) continue;
            for (int sec = 0; sec < 16; ++sec)
            {
                uint64_t h = 0xcbf29ce484222325ULL;
                for (int x = 0; x < 16; ++x)
                    for (int z = 0; z < 16; ++z)
                        for (int y = sec * 16; y < sec * 16 + 16; ++y)
                        {
                            int wx = cx * 16 + x, wz = cz * 16 + z;
                            h = (h ^ (uint64_t)world_get_light(CW, LIGHT_SKY, wx, y, wz)) * 0x100000001b3ULL;
                            h = (h ^ (uint64_t)world_get_light(CW, LIGHT_BLOCK, wx, y, wz)) * 0x100000001b3ULL;
                            h = (h ^ (uint64_t)(world_get_block(CW, wx, y, wz) & 4095)) * 0x100000001b3ULL;
                        }
                printf("CWHASH t=%lld %d %d %d %llx\n", (long long)t, cx, cz, sec, (unsigned long long)h);
            }
        }
}

/* One tick pair (session.c's), then the HUD's per-tick state: what the
 * client does before the pair, and after it (play_tick_begin and
 * play_tick_end, which the library build runs around a pool's tick). */
struct play_tick_carry { int paused; struct surv_stack held_before; };

static void play_tick_begin(int64_t t, const struct act *act, struct play_tick_carry *carry)
{
    live_t = t;
    chat_was_up = CP.screen_chat || CP.screen_sleep;
    /* a batch's left press: the component this client's own drawn chat
     * finds under the pointer must be the one the batch carries (a Java
     * recording's, or this client's own live pick) */
    if (act->has_chat && chat_was_up)
    {
        int left = 0;
        for (int i = 0; i < act->chat.nev; ++i)
            if (act->chat.ev[i].kind == CHAT_EV_PRESS && act->chat.ev[i].button == 0) left = 1;
        char v[GC_OUT_LEN], tx[GC_OUT_LEN];
        int got = left ? live_chat_comp(act->chat.px, act->chat.py, v, sizeof v, tx, sizeof tx) : 0;
        if (left && (got != act->chat.has_comp ||
                     (got && (strcmp(v, act->chat.comp_value) || strcmp(tx, act->chat.comp_text)))))
        {
            fprintf(stderr, "play: tick %lld: the chat click finds %s [%s] where the tape has %s [%s]\n", (long long)t,
                    got ? v : "nothing", got ? tx : "", act->chat.has_comp ? act->chat.comp_value : "nothing",
                    act->chat.has_comp ? act->chat.comp_text : "");
            exit(3);
        }
    }
    particles_live_pin(&surv_fx, live_seed, t);
    drops_before_tick();
    int paused = CP.game_paused;
    /* GuiIngame.updateTick runs at runTick's head, ahead of updateController's
     * packets and the tick's input: it sees the held stack the last tick left */
    const struct surv_stack held_before = CP.sv.inv[CP.hotbar];
    carry->paused = paused;
    carry->held_before = held_before;
}

static void play_tick_end(int64_t t, const struct act *act, const struct play_tick_carry *carry)
{
    (void)act;
    const int paused = carry->paused;
    const struct surv_stack held_before = carry->held_before;
    cents_after_row();
    cw_hash_print(t);
    if (!paused)
    {
        chest_lids_after_row();
        tileents_tick();
    }
    drops_after_tick();
    if (!paused) client_arrows = server_arrows_sent;
    server_arrows_sent = SP.trk_w_valid ? (int)(int8_t)SP.trk_w[5] : 0;
    damaged_blocks_after_row(paused);
    ++hud_update_counter;
    death_screen_ticks = CP.screen_gameover ? death_screen_ticks + 1 : 0;
    credits_ticks = CP.screen_credits ? credits_ticks + 1 : 0;
    sleep_ticks = CP.screen_sleep ? sleep_ticks + 1 : 0;
    /* the server's S02 lines this tick (chatmsg.c: the death message, the
     * bed's refusals, the achievement announcement, build.tooHigh), each
     * the parts GuiNewChat.func_146237_a iterates its component into */
    for (int i = 0; i < CP.s02_n; ++i)
    {
        const char *fmt[CHAT_PARTS], *text[CHAT_PARTS], *click[CHAT_PARTS], *hover[CHAT_PARTS];
        const char *s = CP.s02_text[i];
        int n;
        for (n = 0; n < CP.s02_nparts[i] && n < CHAT_PARTS; ++n)
        {
            fmt[n] = s;
            s += strlen(s) + 1;
            text[n] = s;
            s += strlen(s) + 1;
            click[n] = s;
            s += strlen(s) + 1;
            hover[n] = s;
            s += strlen(s) + 1;
        }
        struct chat_message m;
        chat_message_set(&m, hud_update_counter, CP.s02_id[i], n, fmt, text);
        for (int k = 0; k < n; ++k)
        {
            if (click[k][0]) chat_message_set_click(&m, k, click[k]);
            if (hover[k][0]) chat_message_set_hover(&m, k, hover[k]);
        }
        /* func_146237_a: an id's earlier line goes first; while the chat is
         * open and scrolled, each new line scrolls it one further */
        if (CP.s02_id[i] != 0) chat_remove_id(&live_chat, CP.s02_id[i]);
        int lines = live_chat_message_lines(&m);
        chat_push_message(&live_chat, &m);
        for (int k = 0; k < lines; ++k)
            if (chat_was_up && live_chat_scroll > 0)
            {
                live_chat_scrolled = 1;
                live_chat_scroll_by(1);
            }
    }
    /* the chat screen's batch: its scroll turns and resetScroll, then
     * onGuiClosed's reset when the screen went */
    for (int i = 0; i < CP.chat_fx.nscroll; ++i)
    {
        if (CP.chat_fx.scroll[i] == CHAT_FX_RESET) live_chat_scroll = live_chat_scrolled = 0;
        else live_chat_scroll_by(CP.chat_fx.scroll[i]);
    }
    if (chat_was_up && !CP.screen_chat && !CP.screen_sleep) live_chat_scroll = live_chat_scrolled = 0;
    /* GuiIngame.updateTick: the same item with equal tags and, unless it is
     * damageable (a tool wearing down keeps its clock), the same damage */
    const struct surv_stack *held = &held_before;
    const struct itag *ht = held->count > 0 ? itag_get(held->tag) : NULL;
    int damageable = held->count > 0 && ITEMS[held->item].max_damage > 0 && (ht == NULL || !ht->unbreakable);
    if (held->count <= 0) hud_highlight_ticks = 0;
    else if (held->item != hud_last_item || held->tag != hud_last_tag || (!damageable && held->damage != hud_last_meta))
        hud_highlight_ticks = 40;
    else if (hud_highlight_ticks > 0) --hud_highlight_ticks;
    hud_last_item = held->count > 0 ? held->item : -1;
    hud_last_meta = held->damage;
    hud_last_tag = held->tag;
    hud_highlight_stack = *held;
}

static void play_tick(int64_t t, const struct act *act)
{
    struct play_tick_carry carry;
    play_tick_begin(t, act, &carry);
    if (!session_tick(&SS, t, act))
    {
        fprintf(stderr, "play: tick %lld: %s\n", (long long)t, SS.err);
        exit(3);
    }
    play_tick_end(t, act, &carry);
}

static void hud_options(const char *scene)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "play: no HUD recording %s\n", path); exit(1); }
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) { free(last); last = strdup(line); }
    free(line);
    fclose(f);
    if (!last) { fprintf(stderr, "play: empty HUD recording %s\n", path); exit(1); }
    struct jval *row = json_parse(last);
    const struct jval *hud = json_get(row, "hud");
    int64_t v;
    if (!hud) { fprintf(stderr, "play: no hud in %s\n", path); exit(1); }
    if (json_int(json_get(hud, "sf"), &v)) hud_scale = (int)v;
    if (json_int(json_get(hud, "fancy"), &v)) hud_fancy = (int)v;
    json_free(row);
}

static int hud_xp_cap(int level)
{
    if (level >= 30) return 62 + (level - 30) * 7;
    if (level >= 15) return 17 + (level - 15) * 3;
    return 17;
}

static void live_hud(unsigned char *rgb, int64_t now_tick)
{
    struct hud_live_state h = {0};
    h.width = W; h.height = H; h.scale = hud_scale; h.fancy = play_fancy >= 0 ? play_fancy : hud_fancy;
    h.screen = CP.screen_inventory || CP.screen_gameover;
    h.update_counter = hud_update_counter;
    h.sleep_timer = CP.sv.sleep_timer;
    h.clock_ms = (long long)frame_now_ms;
    h.current_slot = CP.hotbar;
    h.health = CP.sv.health;
    h.previous_health = CP.sv.hud_prev_health;
    h.max_health = 20.0F;
    h.absorption = CP.sv.absorption;
    h.hurt_resistant = CP.sv.hurt_resistant_time;
    h.armor_value = surv_client_armor_value(&CP);
    h.food = CP.sv.food.level;
    h.previous_food = CP.sv.hud_prev_food;
    h.saturation = CP.sv.food.saturation;
    /* getAir: the data watcher the server's S1C keeps current (the client
     * copy of the player never counts its own air down here) */
    h.air = SP.sv.air;
    h.in_water = surv_client_in_water(&CP);
    h.xp = CP.sv.xp_progress;
    h.level = CP.sv.xp_level;
    h.xp_cap = hud_xp_cap(h.level);
    h.show_hud = h.survival = 1;
    /* func_110327_a's styles: the client's own effects (isPotionActive) */
    h.poison = potion_map_get(&CP.sv.potions, POT_POISON) != NULL;
    h.wither = potion_map_get(&CP.sv.potions, POT_WITHER) != NULL;
    h.regen = potion_map_get(&CP.sv.potions, POT_REGENERATION) != NULL;
    h.hunger = potion_map_get(&CP.sv.potions, POT_HUNGER) != NULL;
    /* a ridden living's hearts in place of the food bar: its copy's health
     * and its max */
    if (CP.riding_id != 0)
    {
        struct cent_living *mc = cent_find(CP.riding_id);
        const struct living *ml = cent_server_living(CP.riding_id);
        if (mc && ml)
        {
            h.mount = 1;
            h.mount_health = mc->cli_health;
            h.mount_max = living_max_health((struct living *)ml);
        }
    }
    h.tooltip_enabled = 1;
    h.highlight_ticks = hud_highlight_ticks;
    const struct surv_stack *held = &hud_highlight_stack;
    if (held->count > 0)
    {
        h.highlighted.id = held->item;
        h.highlighted.meta = held->damage;
        h.highlighted.count = held->count;
        h.highlighted.tag = held->tag;
        h.highlight_name = raster_hud_item_name(hud_assets, held->item, held->damage);
    }
    for (int i = 0; i < 9; ++i)
    {
        h.inventory[i].id = CP.sv.inv[i].item;
        h.inventory[i].meta = CP.sv.inv[i].damage;
        h.inventory[i].count = CP.sv.inv[i].count;
        h.inventory[i].tag = CP.sv.inv[i].tag;
        /* GuiIngame.renderInventorySlot: animationsToGo - partialTicks */
        h.inventory[i].pop = (float)CP.inv_anim[i] - live_pt;
    }
    for (int i = 0; i < 4; ++i)
    {
        h.armor[i].id = CP.sv.inv[36 + i].item;
        h.armor[i].meta = CP.sv.inv[36 + i].damage;
        h.armor[i].count = CP.sv.inv[36 + i].count;
        h.armor[i].tag = CP.sv.inv[36 + i].tag;
    }
    int bx = (int)floor(CP.e.pos_x), bz = (int)floor(CP.e.pos_z);
    int by = (int)floor(CP.e.pos_y - (double)CP.e.y_offset + 1.8 * 0.66);
    int light = world_get_full_block_light_value(CW, bx, by, bz, client_skylight_subtracted());
    h.brightness = lbt[light < 0 ? 0 : light > 15 ? 15 : light];
    /* the two full-screen overlays: the pumpkin on the head, first person, and
     * the portal swirl, which the confusion effect suppresses */
    h.pumpkin_overlay = CP.sv.inv[39].count > 0 && CP.sv.inv[39].item == 86;
    h.chat = &live_chat;
    h.third_person = CP.opt_tpv;
    h.chat_open = CP.screen_sleep || CP.screen_chat;
    h.chat_scroll = live_chat_scroll;
    h.chat_scrolled = live_chat_scrolled;
    h.portal = CP.portal.prev + (CP.portal.time - CP.portal.prev) * live_pt;
    h.confusion = potion_map_get(&CP.sv.potions, 9) != NULL;
    /* updateCameraAndRender: renderGameOverlay unless F1 hides it with no
     * screen up (and then renderVignette's smoothing does not run either) */
    if (gui_hidden() && !CP.screen_inventory && !CP.screen_gameover && !CP.screen_sleep && !CP.screen_credits &&
        !CP.screen_chat)
        return;
    float target = 1.0F - h.brightness;
    if (target < 0) target = 0;
    if (target > 1) target = 1;
    hud_vignette = (float)((double)hud_vignette + (double)(target - hud_vignette) * 0.01);
    h.vignette = hud_vignette;
    raster_hud_live(rgb, W, H, hud_assets, &h);
}

static void live_screens(unsigned char *rgb, int pause, int mouse_x, int mouse_y)
{
    /* GuiFurnace reads the client's own furnace, which the S31s write
     * (ContainerFurnace.updateProgressBar: 0 cook, 1 burn, 2 the fuel's total) */
    int burn = 0, fuel = 0, cook = 0;
    if (CP.screen_inventory && cp_gui_container(&CP) && cp_gui_container(&CP)->kind == CONTAINER_FURNACE)
    {
        cook = cp_gui_container(&CP)->furnace_progress[0];
        burn = cp_gui_container(&CP)->furnace_progress[1];
        fuel = cp_gui_container(&CP)->furnace_progress[2];
    }
    /* the screen's extras: the merchant's S2D title, the effect column in
     * the client map's own (HashMap) order, the player preview, the credits */
    static struct gui_screen_extra x;
    memset(&x, 0, sizeof x);
    x.clock_ms = (long long)frame_now_ms;
    const struct container *oc = cp_gui_container(&CP);
    if (oc && oc->kind == CONTAINER_MERCHANT) x.merchant_name = oc->title;
    const struct potion_map *pm = &CP.sv.potions;
    uint8_t pids[POT_COUNT];
    int npids = potion_map_order(pm, pids);
    for (int i = 0; i < npids && x.neffects < 32; ++i)
    {
        const struct potion_effect *n = &pm->eff[pids[i]];
        x.effects[x.neffects++] = (struct gui_effect){n->id, n->amplifier, n->duration, n->duration_max};
    }
    int inv_open = CP.screen_inventory && (!oc || oc->kind == CONTAINER_PLAYER);
    if (inv_open && !inv_was_open) inv_mouse_x = inv_mouse_y = 0.0f;
    inv_was_open = inv_open;
    x.has_preview = inv_open;
    x.preview.mouse_x = inv_mouse_x;
    x.preview.mouse_y = inv_mouse_y;
    x.preview.swing = CP.swing_progress;
    x.preview.y_offset = CP.e.y_offset;
    x.preview.age = CP.ticks_existed;
    x.preview.riding = CP.riding_id != 0;
    x.preview.held = CP.sv.inv[CP.hotbar].count > 0;
    /* the client keeps no limb swing or body yaw: a standing player, the
     * interpolation's start at the preview's own yaw */
    x.preview.prev_body_own = 1;
    x.preview.prev_pitch = CP.prev_rotation_pitch;
    /* RenderPlayer whole over the client player at partial tick 1 */
    static struct mob_render_input preview_mob;
    if (inv_open)
    {
        live_player_render(&preview_mob, 1.0f);
        preview_mob.fire_dy = (float)(CP.e.pos_y - CP.e.bounding_box.min_y);
        /* the lightmap coordinates ItemRenderer.renderItemInFirstPerson left
         * (the player's own cell), which the preview's fire takes */
        if (CP.opt_tpv == 0 && !CP.sv.sleeping && !gui_hidden())
        {
            int bx = (int)floor(CP.e.pos_x), by = (int)floor(CP.e.pos_y), bz = (int)floor(CP.e.pos_z);
            preview_mob.fire_brightness_set = 1;
            preview_mob.fire_brightness = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
                                          (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
        }
        x.preview_mob = &preview_mob;
        x.preview_assets = mob_assets;
        x.preview_lm = frame_lm;
    }
    x.sleep = CP.screen_sleep;
    x.chat = CP.screen_chat;
    if (CP.screen_chat || CP.screen_sleep)
    {
        gui_chat_draw_state(&CP, &x.chat_draw);
        /* the hover at drawScreen's mouse, Mouse.getX and getY */
        x.tip_x = mouse_x;
        x.tip_y = mouse_y;
        x.ntip = live_chat_tooltip(mouse_x * GC_DISPLAY_W / GC_SCREEN_W,
                                   (GC_SCREEN_H - 1 - mouse_y) * GC_DISPLAY_H / GC_SCREEN_H, x.tip, 8);
    }
    x.credits = CP.screen_credits;
    x.credits_time = credits_ticks;
    x.credits_speed = 0.5f;
    x.partial_tick = 1.0f;
    x.user = "Player";
    /* GuiContainer draws the container's slots, whose player half reads the
     * client's own InventoryPlayer and whose cursor is its getItemStack: the
     * client container's copy of both is refreshed only by a click
     * (surv_client_click), so the frame draws a copy filled from them */
    static struct container shown;
    const struct container *drawn = oc;
    if (oc)
    {
        shown = *oc;
        /* the copy's slots and recipe list point into the copy */
        for (int s = 0; s < shown.nslots; ++s)
            shown.slots[s].inv = (struct inv *)((char *)&shown + ((const char *)oc->slots[s].inv - (const char *)oc));
        if (oc->recipes == &oc->client_recipes) shown.recipes = &shown.client_recipes;
        for (int s = 0; s < 40; ++s)
        {
            const struct surv_stack *st = &CP.sv.inv[s];
            shown.player.slot[s] = (struct craft_stack){st->count > 0 ? st->item : -1, st->count,
                                                        st->count > 0 ? st->damage : 0, st->count > 0 ? st->tag : 0};
        }
        const struct surv_stack *cur = &CP.sv.cursor;
        shown.cursor = (struct craft_stack){cur->count > 0 ? cur->item : -1, cur->count,
                                            cur->count > 0 ? cur->damage : 0, cur->count > 0 ? cur->tag : 0};
        drawn = &shown;
        /* the screen's drag while it holds slots: func_146977_a draws each
         * slot's share (func_94525_a over the stack it has, capped at the
         * item's and the slot's limit, the count yellow when capped) on a
         * white square and hides a lone slot; the cursor shows the
         * remainder (func_146980_g) */
        const int *ds;
        int dn, dmode;
        if (CP.screen_inventory && shown.cursor.count > 0 && gui_input_drag(&gin, &ds, &dn, &dmode))
        {
            if (dn == 1 && ds[0] < GUI_DRAG_SLOTS) x.slot_mark[ds[0]] = GUI_MARK_HIDE;
            else
            {
                int remain = shown.cursor.count;
                for (int k = 0; k < dn; ++k)
                {
                    int sl = ds[k];
                    if (sl < 0 || sl >= shown.nslots || sl >= GUI_DRAG_SLOTS) continue;
                    struct craft_stack *have = &shown.slots[sl].inv->slot[shown.slots[sl].index];
                    if (!container_stack_fits(have, &shown.cursor)) continue;
                    int had = have->count > 0 ? have->count : 0;
                    struct craft_stack share = shown.cursor;
                    share.count = (dmode == 0 ? shown.cursor.count / dn : 1) + had;
                    int max = share.item >= 0 ? ITEMS[share.item].max_stack_size : 64, yellow = 0;
                    if (share.count > max) { share.count = max; yellow = 1; }
                    if (share.count > container_slot_limit(&shown, sl))
                    { share.count = container_slot_limit(&shown, sl); yellow = 1; }
                    remain -= share.count - had;
                    *have = share;
                    x.slot_mark[sl] = GUI_MARK_SHARE | (yellow ? GUI_MARK_YELLOW : 0);
                }
                x.drag_on = 1;
                x.drag_remainder = remain;
            }
        }
    }
    raster_gui_live(rgb, W, H, hud_assets, drawn, CP.screen_inventory,
                    CP.screen_gameover, pause, hud_scale, mouse_x, mouse_y,
                    0, death_screen_ticks, burn, fuel, cook, &x);
    if (inv_open) { inv_mouse_x = (float)mouse_x; inv_mouse_y = (float)mouse_y; }
}

/* The GuiAchievement toast, drawn after the open GuiScreen the way vanilla's
 * runGameLoop does (Minecraft.java:1056 renders it last, over the screen).
 * The hud json carries only the toast: raster_hud_draw with hide=1 draws no
 * HUD and no GuiScreen of its own. */
static void live_toast(unsigned char *rgb, int64_t now_tick)
{
    struct hud_live_state h = {0};
    h.width = W; h.height = H; h.scale = hud_scale;
    h.fancy = play_fancy >= 0 ? play_fancy : hud_fancy;
    surv_client_toast_json(&CP, now_tick * 50L, &h.toast_on);
    if (!h.toast_on || CP.sv.stats.toast_ach < 0) return;
    h.toast_l = CP.sv.stats.toast_l;
    h.toast_desc = CP.sv.stats.toast_desc;
    h.toast_title = CP.sv.stats.toast_title;
    h.toast_sub = CP.sv.stats.toast_sub;
    int item = ach_item[CP.sv.stats.toast_ach].item;
    int meta = ach_item[CP.sv.stats.toast_ach].meta;
    h.toast_item.id = item;
    h.toast_item.meta = meta;
    h.toast_item.count = 1;
    h.toast_has_item = 1;
    h.clock_ms = now_tick * 50L;
    char *j = raster_hud_toast_json(&h, hud_assets);
    if (!j) return;
    /* json_parse takes j over; json_free(root) releases it (v->owned). */
    struct jval *root = json_parse(j);
    if (!root) { free(j); fprintf(stderr, "play: toast json invalid\n"); return; }
    raster_hud_draw(rgb, W, H, hud_assets, root);
    json_free(root);
}

/* ---------------------------------------------------------------- the tape */

/* A tape row's act (session.c's parser, the replay gate's): a malformed part
 * is left out. */
static void parse_act(const struct jval *row, struct act *a)
{
    char err[160];
    session_parse_act(row, a, err, sizeof err);
}

static FILE *tape_out;

/* A float as a decimal Float.parseFloat reads back to the same float, never in
 * exponent form (the tape reader takes Gson's plain numbers), always with a
 * point as Float.toString writes it: the oracle's Gson 2.2.4 reads "-0" as the
 * integer 0, so a look pitch of -0.0 written that way replayed as +0.0 (G30
 * of the golden chain, row 4932). */
static void fmt_float(char *b, size_t n, float v)
{
    snprintf(b, n, "%.9g", (double)v);
    if (!strchr(b, 'e') && !strchr(b, 'E'))
    {
        if (!strchr(b, '.') && strlen(b) + 3 <= n) strcat(b, ".0");
        return;
    }
    snprintf(b, n, "%.60f", (double)v);
    char *e = b + strlen(b) - 1;
    while (e > b && *e == '0') *e-- = 0;
    if (*e == '.') e[1] = '0', e[2] = 0;
}

/* The snapshot tape's header, marked inputs_only, then its join ticks. */
/* 1 when the snapshot is the client's join (no row before it carries an
 * act): the join screen closed the tick before, and its setIngameFocus left
 * Minecraft.leftClickCounter at 10000 */
static int start_at_join = 1;

static int tape_begin(const char *dir, const char *out)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/tape.jsonl", dir);
    FILE *in = fopen(p, "r");
    if (!in) { fprintf(stderr, "play: cannot read %s\n", p); return 0; }
    tape_out = fopen(out, "w");
    if (!tape_out) { fprintf(stderr, "play: cannot write %s\n", out); fclose(in); return 0; }
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int first = 1;
    while ((len = getline(&line, &cap, in)) > 0)
    {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (first)
        {
            struct jval *header = json_parse(strdup(line));
            const struct jval *opts = json_get(header, "options");
            int64_t val;
            if (json_int(json_get(opts, "rd"), &val)) play_rd = (int)val;
            if (json_int(json_get(opts, "bob"), &val)) play_bob = (int)val;
            if (json_int(json_get(opts, "clouds"), &val)) play_clouds = (int)val;
            if (json_int(json_get(opts, "fancy"), &val)) play_fancy = (int)val;
            json_free(header);
            char *close = strrchr(line, '}');
            if (!close) { fprintf(stderr, "play: %s has no header\n", p); break; }
            *close = 0;
#ifdef NETHERITE_DEV
            if (nsidecar > 0)
            {
                char *old_false = strstr(line, "\"dev\":false");
                if (old_false)
                {
                    memcpy(old_false + 6, "true", 4);
                    memmove(old_false + 10, old_false + 11, strlen(old_false + 11) + 1);
                }
                int has_dev = strstr(line, "\"dev\":") != NULL;
                char *setup = strstr(line, "\"setup\":");
                if (setup)
                {
                    char *open = strchr(setup, '['), *end = strrchr(line, ']');
                    if (!open || !end || end < open) { fprintf(stderr, "play-dev: bad source setup array\n"); break; }
                    *open = 0;
                    fprintf(tape_out, "%s[", line);
                    write_merged_setup(tape_out);
                    fprintf(tape_out, "]%s", end + 1);
                }
                else
                {
                    fprintf(tape_out, "%s,\"setup\":[", line);
                    write_merged_setup(tape_out);
                    fputc(']', tape_out);
                }
                fprintf(tape_out, "%s,\"inputs_only\":true,\"source\":\"native\"}\n", has_dev ? "" : ",\"dev\":true");
            }
            else
#endif
            fprintf(tape_out, "%s,\"inputs_only\":true,\"source\":\"native\"}\n", line);
            first = 0;
            continue;
        }
        /* a join row has no input: only its tick goes out, none of the
         * oracle's state fields */
        char *tp = strstr(line, "\"t\":");
        long long jt = tp ? strtoll(tp + 4, NULL, 10) : S.tick;
        if (jt >= S.tick) break;
        if (strstr(line, "\"act\"")) start_at_join = 0;
        fprintf(tape_out, "{\"t\":%lld}\n", jt);
    }
    free(line);
    fclose(in);
    fflush(tape_out);
    return !first;
}

/* A JSON string: quotes, backslashes and control characters escaped, the
 * rest as its UTF-8 */
static void json_string_out(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)s; *c; ++c)
    {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

static void tape_row(int64_t t, const struct act *a)
{
    char f[4][96];
    for (int i = 0; i < 4; ++i) fmt_float(f[i], sizeof f[i], a->look[i]);
    fprintf(tape_out, "{\"t\":%lld,\"act\":{\"look\":[%s,%s,%s,%s]",
            (long long)t, f[0], f[1], f[2], f[3]);
    if (a->has_in)
    {
        fprintf(tape_out, ",\"in\":{\"keys\":{");
        int first = 1;
        for (int k = 0; k < K_N; ++k)
            if (a->keys.held[k] || a->keys.presses[k])
            {
                fprintf(tape_out, "%s\"%s\":[%d,%d]", first ? "" : ",", key_desc(k), a->keys.held[k], a->keys.presses[k]);
                first = 0;
            }
        fprintf(tape_out, "},\"hb\":%d,\"focus\":%d,\"lcc\":%d", a->hb, a->focus, a->lcc);
        if (a->ctrl) fprintf(tape_out, ",\"ctrl\":1");
        fprintf(tape_out, "}");
        /* Oracle.endGameInput's opts: every setting, when one changed */
        if (a->has_opts)
            fprintf(tape_out, ",\"opts\":{\"tpv\":%d,\"hide\":%d,\"smooth\":%d,\"rd\":%d,\"dbg\":%d}",
                    a->o_tpv, a->o_hide, a->o_smooth, a->o_rd, a->o_dbg);
    }
    if (a->has_chat)
    {
        /* ["chat", mods, px, py, clip, comp, events] (ChatInput.java) */
        const struct chat_op *c = &a->chat;
        fprintf(tape_out, ",\"gui\":[[\"chat\",%d,%d,%d,", c->mods, c->px, c->py);
        if (c->has_clip)
        {
            char u[GC_MAX * 3 + 1];
            gui_chat_utf8(c->clip, c->clip_len, u);
            json_string_out(tape_out, u);
        }
        else fprintf(tape_out, "null");
        fprintf(tape_out, ",");
        if (c->has_comp)
        {
            fprintf(tape_out, "[\"suggest_command\",");
            json_string_out(tape_out, c->comp_value);
            fprintf(tape_out, ",");
            json_string_out(tape_out, c->comp_text);
            fprintf(tape_out, "]");
        }
        else fprintf(tape_out, "null");
        fprintf(tape_out, ",[");
        for (int i = 0; i < c->nev; ++i)
        {
            const struct chat_ev *e = &c->ev[i];
            if (i) fprintf(tape_out, ",");
            if (e->kind == CHAT_EV_KEY) fprintf(tape_out, "[0,%d,%d]", e->code, e->ch);
            else if (e->kind == CHAT_EV_WHEEL) fprintf(tape_out, "[3,%d]", e->code);
            else fprintf(tape_out, "[%d,%d,%d,%d]", e->kind, e->x, e->y, e->button);
        }
        fprintf(tape_out, "]]]");
    }
    else if (a->clicks || a->trsels || a->gui_close || a->gui_respawn || a->gui_wake)
    {
        /* in the order the client tick applies them: the clicks with the
         * merchant's recipe changes between them, then the close, and the
         * clicks and closes a closed screen's keys made after it */
        const char *sep = "";
        int k = 0;
        fprintf(tape_out, ",\"gui\":[");
        for (int i = 0; i <= a->clicks; ++i)
        {
            for (int j = 0; j < a->trsels && j < 8; ++j)
                if (a->trsel[j].at == i || (i == a->clicks && a->trsel[j].at > i))
                {
                    fprintf(tape_out, "%s[\"trsel\",%d,%d]", sep, a->trsel[j].window, a->trsel[j].index);
                    sep = ",";
                }
            for (; k < a->closes && a->close_at[k] <= i; ++k, sep = ",") fprintf(tape_out, "%s[\"close\"]", sep);
            if (i == a->clicks) break;
            const struct guiclick *c = &a->gui_click[i];
            fprintf(tape_out, "%s[\"click\",%d,%d,%d,%d]", sep, c->window, c->slot, c->button, c->mode);
            sep = ",";
        }
        if (a->gui_close && !a->closes) { fprintf(tape_out, "%s[\"close\"]", sep); sep = ","; }
        if (a->gui_respawn) { fprintf(tape_out, "%s[\"respawn\"]", sep); sep = ","; }
        if (a->gui_wake) fprintf(tape_out, "%s[\"wake\"]", sep);
        fprintf(tape_out, "]");
    }
    fprintf(tape_out, "}}\n");
    fflush(tape_out);
}

/* ---------------------------------------------------------- the render world */

/* The chunks the client draws: a window of chunk records (RW.chunks, NULL
 * for a record never filled), each a copy of the server chunk that shares its
 * bands (chunk_share), refreshed where a tick wrote. The cells are the server
 * world's (its light is the one kept current). An older snapshot, whose loaded
 * set never grows, draws every loaded server chunk in a window built once
 * around them. A v2 snapshot draws the client world's chunks: the window is a
 * square around the player that re-centres as the player walks, and each
 * chunk packet the client applies fills or clears its record. */
static struct rb_world RW;
static int rw_stream;
/* per window slot: the live chunk and its write sequence (world.h wseq)
 * when rw_chunk last made the record equal to it; a chunk whose sequence
 * has not moved since has the same cells, maps and bands, so it is not
 * compared again. Cleared with the window and whenever a slot's record is
 * replaced or emptied. Not with the client light deferred (lightdefer.h):
 * its results land in the chunks at the syncs, whose sequence is the
 * device's. */
static struct rw_seen { const struct chunk *c; uint64_t wseq; } *rw_seen;
/* A client world whose write sequence has not moved cannot have changed
 * any chunk the frame record compares. The window reset invalidates this
 * shortcut even when the world itself did not change. */
static const struct world *rw_scanned_world;
static uint64_t rw_scanned_wseq;

static void rw_seen_drop(struct chunk **slot)
{
    if (rw_seen && RW.chunks && slot >= RW.chunks && slot < RW.chunks + (size_t)RW.rows * RW.rows)
        rw_seen[slot - RW.chunks].c = NULL;
}

static void rw_seen_new(void)
{
    free(rw_seen);
    rw_seen = RW.chunks ? calloc((size_t)RW.rows * RW.rows, sizeof *rw_seen) : NULL;
    rw_scanned_world = NULL;
}
/* where the renderer's sections are meshed (raster_live_device_mesh):
 * --obs-mesh 2 dumps the device mesher's feed with each frame, the view
 * (pv_config.device_mesh) sends it to the device */
static int play_dmesh;
/* --render-prec (pv_config.render_prec): the renderer's precision
 * (engine/raster_prec.h: exact, fast or fast:STAGES), the terrain's and the
 * entities' */
static int play_prec;
/* --mesh-check (pv_config.mesh_check): every section pass the mesh key
 * spares is meshed anyway and compared (raster_live_mesh_check) */
static int play_mesh_check;

/* The drawn world is the client's own (its light as WorldClient keeps it)
 * while every one of its light checks ran at the oracle's cell (WorldClient.rand
 * known, lane/clientgate's d.cw); a client world whose Random was unknown for
 * a check (an older recording) went through checks at cells nobody knows, so
 * it draws the server's world. */
static int rw_from_cw(void)
{
    return CW && CW->dim == view_dim && SS.cp.cw_rand_known && !SS.cp.cw_check_missed;
}

static struct chunk **rw_slot(int cx, int cz)
{
    int dx = cx - (RW.origin_cx - RW.margin), dz = cz - (RW.origin_cz - RW.margin);
    if (dx < 0 || dx >= RW.rows || dz < 0 || dz >= RW.rows) return NULL;
    return &RW.chunks[(size_t)dx * RW.rows + dz];
}

/* a record takes the chunk whole: its bands shared, its maps copied (a
 * band it did not hold named to the renderer's key cache) */
static void rw_take(struct chunk **slot, const struct chunk *c)
{
    rw_seen_drop(slot);
    if (*slot == NULL) *slot = chunk_new();
    const struct chunk_sec *had[16];
    for (int s = 0; s < 16; ++s) had[s] = chunk_sec_at(*slot, s);
    chunk_share(*slot, c);
    memset(&(*slot)->tes, 0, sizeof (*slot)->tes);
    for (int s = 0; s < 16; ++s)
        if (chunk_sec_at(*slot, s) != had[s]) raster_live_band_taken(RL, chunk_sec_at(*slot, s));
}

static void rw_clear(struct chunk **slot)
{
    rw_seen_drop(slot);
    chunk_free(*slot);
    *slot = NULL;
}

static void rw_put(const struct chunk *c)
{
    struct chunk **slot = rw_slot(c->cx, c->cz);
    if (slot) rw_take(slot, c);
}

static void rw_free(void)
{
    for (int i = 0; RW.chunks && i < RW.rows * RW.rows; ++i) rw_clear(&RW.chunks[i]);
    free(RW.chunks);
    RW.chunks = NULL;
    rw_seen_new();
}

/* The streamed window: the render distance's square plus the view distance's
 * slack, so the chunks the client holds while the managed position lags stay
 * inside, and a re-centre happens once every few chunks walked. */
static int rw_stream_margin(void)
{
    int r = render_rd() > SR.view ? render_rd() : SR.view;
    return r + 4;
}

static void rw_stale_around(int cx, int cz)
{
    if (!RL) return;
    for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz)
        for (int s = 0; s < 16; ++s) raster_live_stale(RL, cx + dx, s, cz + dz);
}

/* Move the streamed window's centre to (cx, cz): the records that stay are
 * copied across, the cached meshes follow them. */
static void rw_recentre(int cx, int cz)
{
    int old_cx = RW.origin_cx, old_cz = RW.origin_cz, old_margin = RW.margin, old_rows = RW.rows;
    struct chunk **old = RW.chunks;
    RW.chunks = calloc((size_t)RW.rows * RW.rows, sizeof *RW.chunks);
    if (!RW.chunks) { fprintf(stderr, "play: cannot allocate the render world\n"); exit(1); }
    RW.origin_cx = cx;
    RW.origin_cz = cz;
    for (int dx = 0; dx < old_rows; ++dx)
        for (int dz = 0; dz < old_rows; ++dz)
        {
            struct chunk **from = &old[(size_t)dx * old_rows + dz];
            int nx = old_cx - old_margin + dx - (RW.origin_cx - RW.margin);
            int nz = old_cz - old_margin + dz - (RW.origin_cz - RW.margin);
            if (nx < 0 || nx >= RW.rows || nz < 0 || nz >= RW.rows) { rw_clear(from); continue; }
            RW.chunks[(size_t)nx * RW.rows + nz] = *from;
        }
    free(old);
    rw_seen_new();
    if (RL) raster_live_rebase(RL, old_cx, old_cz, old_margin, old_rows);
    /* the client chunks the old window could not hold */
    for (int x = RW.origin_cx - RW.margin; x <= RW.origin_cx + RW.margin; ++x)
        for (int z = RW.origin_cz - RW.margin; z <= RW.origin_cz + RW.margin; ++z)
        {
            if (abs(x - old_cx) <= old_margin && abs(z - old_cz) <= old_margin) continue;
            if (!world_chunk(CW, x, z)) continue;
            const struct chunk *c = rw_from_cw() ? world_chunk(CW, x, z) : world_chunk(view_world(), x, z);
            rw_put(c ? c : world_chunk(CW, x, z));
            rw_stale_around(x, z);
        }
}

/* A chunk packet the client just applied: the record takes the chunk's
 * bytes, or goes back to empty, and every section that reads it re-meshes. */
static void rw_on_chunk(void *ctx, char kind, int cx, int cz)
{
    (void)ctx;
    if (!rw_stream) return;
    int rx = cx - (RW.origin_cx - RW.margin), rz = cz - (RW.origin_cz - RW.margin);
    if (rx < 0 || rx >= RW.rows || rz < 0 || rz >= RW.rows) return;
    if (kind == 'l')
    {
        const struct chunk *c = rw_from_cw() ? world_chunk(CW, cx, cz) : NULL;
        if (!c) c = world_chunk(view_world(), cx, cz);
        if (!c) c = world_chunk(CW, cx, cz);
        if (!c) return;
        rw_put(c);
    }
    else rw_clear(rw_slot(cx, cz));
    rw_stale_around(cx, cz);
}

static int rw_build_window(void);

static int rw_build(void)
{
    int ok = rw_build_window();
    /* the Nether's and the End's providers have no sky; the flower pots'
     * plants from the live world's tile entities */
    RW.no_sky = view_dim != 0;
    RW.live = view_world();
    return ok;
}

static int rw_build_window(void)
{
    const struct world *vw = view_world();
    if (SR.stream_chunks)
    {
        memset(&RW, 0, sizeof RW);
        rw_stream = 1;
        RW.margin = rw_stream_margin();
        RW.rows = 2 * RW.margin + 1;
        RW.origin_cx = (int)floor(CP.e.pos_x / 16.0);
        RW.origin_cz = (int)floor(CP.e.pos_z / 16.0);
        RW.chunks = calloc((size_t)RW.rows * RW.rows, sizeof *RW.chunks);
        rw_seen_new();
        return RW.chunks != NULL;
    }
    int x0 = INT_MAX, x1 = INT_MIN, z0 = INT_MAX, z1 = INT_MIN;
    for (size_t i = 0; i < vw->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(vw->slot[i]);
        if (!c) continue;
        if (c->cx < x0) x0 = c->cx;
        if (c->cx > x1) x1 = c->cx;
        if (c->cz < z0) z0 = c->cz;
        if (c->cz > z1) z1 = c->cz;
    }
    if (x0 > x1) return 0;
    memset(&RW, 0, sizeof RW);
    RW.origin_cx = (int)floor((x0 + x1) / 2.0);
    RW.origin_cz = (int)floor((z0 + z1) / 2.0);
    int m = RW.origin_cx - x0;
    if (x1 - RW.origin_cx > m) m = x1 - RW.origin_cx;
    if (RW.origin_cz - z0 > m) m = RW.origin_cz - z0;
    if (z1 - RW.origin_cz > m) m = z1 - RW.origin_cz;
    RW.margin = m + 1;
    RW.rows = 2 * RW.margin + 1;
    RW.chunks = calloc((size_t)RW.rows * RW.rows, sizeof *RW.chunks);
    if (!RW.chunks) return 0;
    rw_seen_new();
    for (size_t i = 0; i < vw->cap; ++i)
        if (vw->slot[i])
        {
            const struct chunk *sc = chunk_ptr(vw->slot[i]);
            const struct chunk *cc = rw_from_cw() ? world_chunk(CW, sc->cx, sc->cz) : NULL;
            rw_put(cc ? cc : sc);
        }
    return 1;
}

/* After a tick: every chunk within one of a write (light spreads into
 * neighbours) is compared with its record, band by band: a band the record
 * still shares with the live chunk is the same storage, so unchanged; any
 * other is compared column by column (a band with no storage is zeros), then
 * the record shares the live band. A section's mesh reads one block past its
 * edges (faces, smooth light, fluid heights), so a change re-meshes its own
 * section, plus a neighbour only across a face, edge or corner the changed
 * cells touch. A changed height map or section mask re-meshes the whole
 * column and takes the chunk whole: an absent section's sky light comes from
 * the height map. */
static int rw_writes, rw_chunks, rw_diff_secs, rw_column_chunks;

static void rw_chunk(const struct chunk *c)
{
    struct chunk **slot = rw_slot(c->cx, c->cz);
    if (!slot) return;
    /* a chunk the client does not hold stays empty */
    if (rw_stream && !world_chunk(CW, c->cx, c->cz)) return;
    struct rw_seen *seen = rw_seen ? &rw_seen[slot - RW.chunks] : NULL;
    if (*slot != NULL && seen && seen->c == c && seen->wseq == c->wseq && nw_env->light.defer == NULL)
    {
        ++rw_chunks;
        return;
    }
    if (*slot == NULL) *slot = chunk_new();   /* the record of zeros */
    struct chunk *rec = *slot;
    ++rw_chunks;
    if (memcmp(rec->height, c->height, sizeof c->height) || rec->mask != c->mask)
    {
        rw_take(slot, c);
        for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz)
            for (int s = 0; s < 16; ++s) raster_live_stale(RL, c->cx + dx, s, c->cz + dz);
        ++rw_column_chunks;
        if (seen) *seen = (struct rw_seen){c, c->wseq};
        return;
    }
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *sec = chunk_sec_at(c, s), *was = chunk_sec_at(rec, s);
        if (sec == was) continue;
        int lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0}, any = 0;   /* x, y, z borders touched */
        uint16_t cid[16], rid[16];
        uint8_t cme[16], csk[16], cbl[16], rme[16], rsk[16], rbl[16];
        /* both bands there: the same bytes are no change, and a column's
         * cells are 16 id bytes and 8 bytes of each nibble array */
        int both = sec && was, hi_same = both && !sec->ids_hi && !was->ids_hi;
        if (hi_same && !memcmp(sec->ids, was->ids, sizeof sec->ids) && !memcmp(chunk_sec_metas(sec), chunk_sec_metas(was), SEC_NIB_BYTES) &&
            !memcmp(chunk_sec_sky(sec), chunk_sec_sky(was), SEC_NIB_BYTES) && !memcmp(chunk_sec_blocklight(sec), chunk_sec_blocklight(was), SEC_NIB_BYTES))
        {
            /* the same bytes: the old band's key hashes are the new one's */
            raster_live_band_same(RL, was, sec);
            chunk_share_band(rec, c, s);
            continue;
        }
        for (int col = 0; col < 256; ++col)
        {
            if (hi_same)
            {
                int o = col << 4, b = o >> 1;
                uint64_t i0, i1, j0, j1, m0, m1, s0, s1, l0, l1;
                memcpy(&i0, sec->ids + o, 8); memcpy(&j0, was->ids + o, 8);
                memcpy(&i1, sec->ids + o + 8, 8); memcpy(&j1, was->ids + o + 8, 8);
                memcpy(&m0, chunk_sec_metas(sec) + b, 8); memcpy(&m1, chunk_sec_metas(was) + b, 8);
                memcpy(&s0, chunk_sec_sky(sec) + b, 8); memcpy(&s1, chunk_sec_sky(was) + b, 8);
                memcpy(&l0, chunk_sec_blocklight(sec) + b, 8); memcpy(&l1, chunk_sec_blocklight(was) + b, 8);
                if (i0 == j0 && i1 == j1 && m0 == m1 && s0 == s1 && l0 == l1) continue;
            }
            chunk_sec_column_out(sec, col, cid, cme, csk, cbl);
            chunk_sec_column_out(was, col, rid, rme, rsk, rbl);
            if (!memcmp(rid, cid, 32) && !memcmp(rme, cme, 16) && !memcmp(rsk, csk, 16) && !memcmp(rbl, cbl, 16))
                continue;
            int x = col >> 4, z = col & 15;
            for (int y = 0; y < 16; ++y)
                if (rid[y] != cid[y] || rme[y] != cme[y] || rsk[y] != csk[y] || rbl[y] != cbl[y])
                {
                    any = 1;
                    lo[0] |= x == 0; hi[0] |= x == 15;
                    lo[1] |= y == 0; hi[1] |= y == 15;
                    lo[2] |= z == 0; hi[2] |= z == 15;
                }
        }
        chunk_share_band(rec, c, s);
        raster_live_band_taken(RL, sec);
        if (!any) continue;
        ++rw_diff_secs;
        for (int dx = -lo[0]; dx <= hi[0]; ++dx)
            for (int dy = -lo[1]; dy <= hi[1]; ++dy)
                for (int dz = -lo[2]; dz <= hi[2]; ++dz)
                    raster_live_stale(RL, c->cx + dx, s + dy, c->cz + dz);
    }
    memcpy(rec->biome, c->biome, sizeof rec->biome);
    if (seen) *seen = (struct rw_seen){c, c->wseq};
}

/* A flower pot whose plant changed (BlockFlowerPot.onBlockActivated's
 * markBlockForUpdate when the metadata did not move): its section re-meshes. */
static struct rw_pot { int x, y, z, item, data, live; } rw_pots[256];
static int nrw_pots;

static void rw_pots_check(void)
{
    struct world *w = view_world();
    for (int i = 0; i < nrw_pots; ++i) rw_pots[i].live = 0;
    for (int i = 0; w && i < w->te_n; ++i)
    {
        const struct tile_entity *te = w->te_list[i];
        if (!te || te->invalid || te->kind != TE_FLOWER_POT) continue;
        struct rw_pot *p = NULL;
        for (int k = 0; k < nrw_pots && !p; ++k)
            if (rw_pots[k].x == te->x && rw_pots[k].y == te->y && rw_pots[k].z == te->z) p = &rw_pots[k];
        if (!p)
        {
            if (nrw_pots == 256) continue;
            p = &rw_pots[nrw_pots++];
            *p = (struct rw_pot){te->x, te->y, te->z, te->u.pot.item, te->u.pot.data, 0};
        }
        p->live = 1;
        if (p->item == te->u.pot.item && p->data == te->u.pot.data) continue;
        p->item = te->u.pot.item;
        p->data = te->u.pot.data;
        raster_live_stale(RL, te->x >> 4, te->y >> 4, te->z >> 4);
    }
    int k = 0;
    for (int i = 0; i < nrw_pots; ++i) if (rw_pots[i].live) rw_pots[k++] = rw_pots[i];
    nrw_pots = k;
}

static void rw_after_tick(void)
{
    rw_pots_check();
    /* the drawn square (the render distance around the camera) keeps a
     * chunk of slack inside the streamed window */
    if (rw_stream)
    {
        int pcx = (int)floor(CP.e.pos_x / 16.0), pcz = (int)floor(CP.e.pos_z / 16.0);
        int slack = RW.margin - render_rd() - 1;
        if (abs(pcx - RW.origin_cx) > slack || abs(pcz - RW.origin_cz) > slack) rw_recentre(pcx, pcz);
    }
    const struct servertick *st = view_st();
    /* the tick's writes, the extra ones, and a dev op's at the tick's head
     * (dev_end keeps them for the client's flush) */
    int ndev = SR.dev_writes_dim == view_dim ? SR.ndev_writes : 0;
    int total = st->nwrites + SR.d->nextra + ndev;
    int from_cw = rw_from_cw();
    static int64_t done[512];
    int ndone = 0;
    rw_writes = total; rw_chunks = rw_diff_secs = rw_column_chunks = 0;
    /* a world drawn from the client's is compared whole below: the writes'
     * chunks need no pass of their own */
    for (int i = 0; !from_cw && i < total; ++i)
    {
        const struct st_write *wr = i < st->nwrites ? &st->writes[i]
            : i < st->nwrites + SR.d->nextra ? &SR.d->extra[i - st->nwrites]
            : &SR.dev_writes[i - st->nwrites - SR.d->nextra];
        if (wr->y < 0 || wr->y >= 256) continue;
        int cx = wr->x >> 4, cz = wr->z >> 4;
        for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz)
        {
            int64_t key = chunk_key(cx + dx, cz + dz);
            int seen = 0;
            for (int k = 0; k < ndone; ++k) if (done[k] == key) { seen = 1; break; }
            if (seen) continue;
            if (ndone < 512) done[ndone++] = key;
            const struct chunk *c = world_chunk(view_world(), cx + dx, cz + dz);
            if (c) rw_chunk(c);
        }
    }
    /* the client world's own light moves without a write (its relight
     * rotation, recheckGaps, the light check): every chunk is compared */
    int scan_cw = from_cw && (nw_env->light.defer != NULL ||
                              rw_scanned_world != CW || rw_scanned_wseq != CW->wseq);
    for (int i = 0; scan_cw && i < RW.rows * RW.rows; ++i)
    {
        int cx = RW.origin_cx - RW.margin + i / RW.rows, cz = RW.origin_cz - RW.margin + i % RW.rows;
        const struct chunk *c = world_chunk(CW, cx, cz);
        if (c) rw_chunk(c);
    }
    rw_scanned_world = from_cw && nw_env->light.defer == NULL ? CW : NULL;
    if (rw_scanned_world) rw_scanned_wseq = CW->wseq;
}

/* A frame recorder that checks a texture's copy again only after a write
 * or a free that could have moved it (raster_rec_free_count). */
static struct raster_rec *play_rec_new(void)
{
    struct raster_rec *r = raster_rec_new();
    raster_rec_free_count(image_free_count_at);
    return r;
}

/* ------------------------------------------------------------- frame inputs */

static float fc1, fc2, fmh = 1.0F, fmhp = 1.0F;
static int ruc;

/* TextureMap.updateAnimations' sprites and dials, from a recording's dump
 * (the assets' own state/anim.json, else anim_liquids'), and
 * EntityRenderer.updateTorchFlicker's floats. Both draw on the client stream
 * of SR.det, which the live client does not keep exact, so the phases are
 * vanilla's arithmetic over approximate draws. */
static struct texanim play_anim;
static struct texanim_state play_anim_st;
static int play_anim_ok, play_anim_started, play_er_started;
static struct rs_flicker play_flicker;

static void play_anim_open(const char *assets)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/state", assets);
    play_anim_ok = texanim_open(p, &play_anim, &play_anim_st) == 0 ||
                   texanim_open("out/java/rendertick/anim_liquids/state", &play_anim, &play_anim_st) == 0;
    if (play_anim_ok) texanim_live_set(&play_anim, &play_anim_st);
}

static void light_table(void)
{
    /* WorldProvider.generateLightBrightnessTable, WorldProviderHell's with
     * its 0.1 floor */
    float floor_ = view_dim == -1 ? 0.1F : 0.0F;
    for (int i = 0; i <= 15; ++i)
    {
        float v = 1.0F - (float)i / 15.0F;
        lbt[i] = (1.0F - v) / (v * 3.0F + 1.0F) * (1.0F - floor_) + floor_;
    }
}

/* After a tick: the client moved to another dimension, so the render world
 * becomes that dimension's chunks and the renderer starts over on it. */
static void view_follow(void)
{
    if (CP.dimension == view_dim) return;
    view_dim = CP.dimension;
    raster_live_free(RL);
    rw_free();
    if (!rw_build()) { fprintf(stderr, "play: the new dimension has no chunks\n"); exit(1); }
    RL = raster_live_new(view_assets, &RW, W, H, view_threads);
    if (play_dmesh) raster_live_device_mesh(RL, play_dmesh);
    raster_live_set_prec(RL, play_prec);
    raster_live_mesh_check(RL, play_mesh_check);
    raster_rec_verify(play_mesh_check);
    /* the particles read block colours through the renderer's mesher: the
     * old one is gone (a digging particle after a respawn from the Nether
     * read the freed world, exit 139) */
    surv_fx.mesher = raster_live_mesher(RL);
    light_table();
    fprintf(stderr, "play: into dimension %d, render world %dx%d\n", view_dim, RW.rows, RW.rows);
}

/* java.awt.Color.HSBtoRGB. */
static int hsb_to_rgb(float hue, float sat, float bri)
{
    int r = 0, g = 0, b = 0;
    if (sat == 0.0F) r = g = b = (int)(bri * 255.0F + 0.5F);
    else
    {
        float h = (hue - (float)floor(hue)) * 6.0F;
        float f = h - (float)floor(h);
        float p = bri * (1.0F - sat);
        float q = bri * (1.0F - sat * f);
        float t = bri * (1.0F - (sat * (1.0F - f)));
        switch ((int)h)
        {
        case 0: r = (int)(bri * 255.0F + 0.5F); g = (int)(t * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
        case 1: r = (int)(q * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
        case 2: r = (int)(p * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(t * 255.0F + 0.5F); break;
        case 3: r = (int)(p * 255.0F + 0.5F); g = (int)(q * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
        case 4: r = (int)(t * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
        case 5: r = (int)(bri * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(q * 255.0F + 0.5F); break;
        }
    }
    return (int)(0xff000000u | (unsigned)(r << 16) | (unsigned)(g << 8) | (unsigned)b);
}

/* BiomeGenBase.getSkyColorByTemp. */
static int sky_color_by_temp(float f)
{
    f /= 3.0F;
    if (f < -1.0F) f = -1.0F;
    if (f > 1.0F) f = 1.0F;
    return hsb_to_rgb(0.62222224F - f * 0.05F, 0.5F + f * 0.1F, 1.0F);
}

/* ItemRenderer's itemToRender and equip animation. A stack's identity is its
 * surv_stack gen (the writes that replaced the object), which is what
 * updateEquippedItem's == compares. */
/* ItemRenderer at a snapshot: the progress risen to 1 since the join (an
 * item changed in the rows just before it would still be moving) */
static struct { int present, item, damage, gen, slot, tag; float eq, peq; } equip = {0, 0, 0, 0, -1, 0, 1.0F, 1.0F};

/* ItemRenderer.updateEquippedItem, from EntityRenderer.updateRenderer. */
static void equip_tick(void)
{
    if (CP.equip_reset) { equip.eq = 0.0F; CP.equip_reset = 0; }   /* resetEquippedProgress */
    equip.peq = equip.eq;
    const struct surv_stack *cur = &CP.sv.inv[CP.hotbar];
    int present = cur->count > 0;
    /* itemToRender is a reference: while it is the held stack itself, a
     * change made in place (a tool's own wear on the client) is its change
     * too, so a later S2F copy with that damage takes over without a
     * re-equip */
    if (present && equip.present && cur->gen == equip.gen) { equip.item = cur->item; equip.damage = cur->damage; equip.tag = cur->tag; }
    int same = equip.slot == CP.hotbar && present && equip.present && cur->gen == equip.gen;
    if (!equip.present && !present) same = 1;
    if (present && equip.present && cur->gen != equip.gen && cur->item == equip.item &&
        cur->damage == equip.damage)
    {
        equip.gen = cur->gen;
        equip.tag = cur->tag;
        same = 1;
    }
    float d = (same ? 1.0F : 0.0F) - equip.eq;
    if (d < -0.4F) d = -0.4F;
    if (d > 0.4F) d = 0.4F;
    equip.eq += d;
    if (equip.eq < 0.1F)
    {
        equip.present = present;
        equip.item = cur->item;
        equip.damage = cur->damage;
        equip.tag = cur->tag;
        equip.gen = cur->gen;
        equip.slot = CP.hotbar;
    }
}

/* EntityRenderer.updateRenderer's fog smoothing and updateFovModifierHand, and
 * RenderGlobal.updateClouds, once per client tick. */
static float yaw;   /* defined with the look below */
static void tileents_tick(void);

/* runTick's TextureManager.tick and EntityRenderer.updateRenderer, from
 * the client player's tick (update_renderer): after the input loops, before
 * the living update, so the FOV's sprint, the fog's light and the equip
 * animation's current item are the ones vanilla reads there */
static void live_update_renderer(struct client_player *p)
{
    (void)p;
    /* theWorld is the client's world as the tick has it: a respawn's S07
     * has already replaced it (view_dim follows after the tick). Off the
     * surface each dial takes a Math.random (the engine's own
     * cp_renderer_draws reads the same world), and the fog's light is read
     * in that dimension: the old one's server world at the new position
     * would load (generate) a chunk there */
    const int cdim = CP.e.world ? CP.e.world->dim : view_dim;
    /* TextureManager.tick, then updateRenderer's torch flicker. Vanilla
     * registers both dials whatever the scene recorded: a dial the loaded
     * animation lacks (or no animation at all) still takes its Math.random
     * off the surface, as the engine's own cp_renderer_draws spends them */
    int dials = 0;
    for (int m = 0; play_anim_ok && m < TEXANIM_MAPS; ++m)
        for (int i = 0; i < play_anim.n[m]; ++i)
            dials += play_anim.s[m][i].kind == TEXANIM_CLOCK || play_anim.s[m][i].kind == TEXANIM_COMPASS;
    if (CP.e.world && cdim != 0)
        for (int k = dials; k < 2; ++k) (void)det_math_random_role(&SR_DET(&SR), DET_CLIENT);
    if (play_anim_ok) {
        struct texanim_world w = {.world = CP.e.world != NULL, .surface = cdim == 0,
            .celestial = renderstate_celestial(cdim, CP.cw_day, 1.0F),
            .spawn_x = SR.d->spawner.spawn_x, .spawn_z = SR.d->spawner.spawn_z,
            .px = CP.e.pos_x, .pz = CP.e.pos_z, .yaw = (double)yaw};
        /* the oracle's sprites have taken one update per client tick from
         * the stitch (a replay's client ticks from tape tick 0, a pool
         * member rewinds them to frame 0): the recorded scene's phase is
         * not the session's, so the first tick starts them over as if
         * ticks 0 to t - 1 had run */
        if (!play_anim_started)
        {
            play_anim_started = 1;
            texanim_sprites_from_start(&play_anim, &play_anim_st, live_t);
        }
        texanim_tick(&play_anim, &play_anim_st, &w, &SR_DET(&SR));
        if (RL) raster_live_animate(RL, &play_anim, &play_anim_st);
    }
    {
        /* the eight draws still spend the shared stream; the values are the
         * flicker's own */
        double v[8];
        det_rng *r = det_pin_at(&pin_flicker, live_t);
        for (int i = 0; i < 8; ++i)
        {
            (void)det_math_random_role(&SR_DET(&SR), DET_CLIENT);
            v[i] = det_rng_double(r);
        }
        renderstate_torch_flicker_step(&play_flicker, v);
    }
    /* updateFovModifierHand: getFOVMultiplier over flying, the movementSpeed
     * attribute (the sprint as the physics carries it, then the speed and
     * slowness effects' operation-2 modifiers the S20 brings) and a bow's
     * draw */
    {
        struct cs_fov_in fi = {CP.is_flying, 0.1F, CP.move_speed, 0, 0};
        for (int id = 1; id <= 2; ++id)
        {
            const struct potion_effect *pe = potion_map_get(&CP.sv.potions, id);
            struct cs_attr_mod m;
            if (pe && clientstate_potion_speed_mod(id, pe->amplifier, &m)) fi.move_speed *= 1.0 + m.amount;
        }
        int us = CP.sv.using_slot;
        if (us >= 0 && CP.sv.inv[us].count > 0 && CP.sv.inv[us].item == 261)
        {
            fi.bow = 1;
            fi.use_duration = 72000 - CP.sv.using_count;
        }
        clientstate_fov_hand(&fmh, &fmhp, clientstate_fov_multiplier(&fi));
    }
    struct world *lw = cdim == -1 ? &SR.hell.world : cdim == 1 ? &SR.sky.world : &SR.pop.world;
    int level = world_get_full_block_light_value(lw, (int)floor(CP.e.pos_x), (int)floor(CP.e.pos_y),
                                                 (int)floor(CP.e.pos_z), cdim == -1 ? 11 : 0);
    float b = lbt[level < 0 ? 0 : level > 15 ? 15 : level];
    float f1 = (float)render_rd() / 16.0F;
    float f2 = b * (1.0F - f1) + f1;
    /* the oracle's EntityRenderer has run updateRenderer once per client
     * tick from tape tick 1 (fogColor1 from 0, rendererUpdateCount and
     * RenderGlobal's cloud counter from 0): the first tick here starts them
     * as if ticks 1 to t - 1 had run, the fog's light at this place */
    if (!play_er_started)
    {
        play_er_started = 1;
        for (int64_t i = 1; i < live_t - 1; ++i) fc1 += (f2 - fc1) * 0.1F;
        ruc = cloud_tick = live_t > 1 ? (int)(live_t - 2) : 0;
    }
    fc2 = fc1;
    fc1 += (f2 - fc1) * 0.1F;
    ++ruc;
    equip_tick();
    /* RenderGlobal.updateClouds */
    ++cloud_tick;
}

/* After the tick pair: the tile entities' own animations (the chest lids,
 * updateEntities' tile entity pass). */
static void renderer_tick(void)
{
    /* the tile entities tick with the row (play_tick) */
}

/* The camera's look, carried the way EntityPlayerSP's four fields are: the tick
 * sets them, the frame's mouse turns them with Entity.setAngles. */
static float yaw, pitch, pyaw, ppitch;

static void draw_hand(unsigned char *rgb, const struct rs_out *o, float pt);
static void equip_item(const struct equip_slot *sl, struct thing_item *out);

/* EntityRenderer.renderRainSnow, then (with HAND) renderHand's hand, then
 * ItemRenderer.renderOverlays over the live frame: the world, the mesher and
 * the textures come from the renderer, the rest from the client's own state. */
static void live_worldfx(const struct rs_in *in, const struct rs_out *o, float pt, unsigned char *rgb,
                         int hand)
{
    struct worldfx_in v = {0};
    v.w = W; v.h = H;
    v.proj = o->proj; v.mv = o->mv;
    double cam[3] = {o->camx, o->camy, o->camz};
    v.cam = cam;
    v.fog = o->gfogc;
    v.fogs = o->gfogs; v.foge = o->gfoge; v.fogd = o->gfogd; v.fogm = o->gfogm;
    v.lm = (const uint32_t *)o->lm;
    memcpy(frame_lm, o->lm, sizeof frame_lm);
    v.depth = raster_live_depth_mut(RL);
    v.rgb = NULL;                       /* filled by the caller's frame buffer */
    v.triangles = NULL; v.samples = NULL;
    v.ruc = ruc;
    v.fancy = play_fancy >= 0 ? play_fancy : fancy_assets;
    v.pt = pt;
    v.eye_x = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt;
    v.eye_y = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt;
    v.eye_z = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt;
    v.eye_now_x = CP.e.pos_x; v.eye_now_y = CP.e.pos_y; v.eye_now_z = CP.e.pos_z;
    const struct servertick *st = view_st();
    v.rain = v.prain = st->prev_raining_strength;   /* WorldClient's: see build_rs */
    v.mat = in->mat;
    /* Entity.isBurning on the client: its own fire (moveEntity's touch of
     * fire or lava this tick; the base tick zeroes it) or the server's flag */
    v.burning = CP.e.fire > 0 || CP.sv.fire > 0;
    v.underwater = surv_client_in_water(&CP);
    v.yaw = yaw; v.pitch = pitch;
    v.far = (float)(render_rd() * 16);
    struct rs_in hin = *in;
    float hand_mv[16];
    hin.death = CP.sv.death_time;
    renderstate_hand_camera(&hin, v.hand_proj, hand_mv);   /* the overlays take its projection only */

    /* Entity.getBrightness(pt): the light at 0.66 of the box's height */
    int bx = (int)floor(CP.e.pos_x), bz = (int)floor(CP.e.pos_z);
    int by = (int)floor(CP.e.pos_y - (double)CP.e.y_offset + 1.8 * 0.66);
    int light = world_get_full_block_light_value(CW, bx, by, bz, client_skylight_subtracted());
    v.brightness = lbt[light < 0 ? 0 : light > 15 ? 15 : light];

    if (worldfx_is_inside_opaque(&RW, CP.e.pos_x, CP.e.pos_y - (double)CP.e.y_offset,
                                 CP.e.pos_z, 0.6, 1.62))
        v.inside_count = worldfx_inside_block(&RW, raster_live_mesher(RL), CP.e.pos_x,
            CP.e.pos_y - (double)CP.e.y_offset, CP.e.pos_z, 0.6, 1.8, v.inside_uv);

    v.rgb = rgb;
    raster_live_weather(RL, &v);
    /* renderHand: the item in first person only, not asleep, not under F1;
     * ItemRenderer.renderOverlays in first person and awake */
    int first = CP.opt_tpv == 0 && !CP.sv.sleeping;
    if (hand && first && !gui_hidden()) draw_hand(rgb, o, pt);
    if (first) raster_live_overlays(RL, &v);
}

static void set_angles(float dx, float dy)
{
    float p0 = pitch, y0 = yaw;
    yaw = (float)((double)yaw + (double)dx * 0.15);
    pitch = (float)((double)pitch - (double)dy * 0.15);
    if (pitch < -90.0F) pitch = -90.0F;
    if (pitch > 90.0F) pitch = 90.0F;
    ppitch += pitch - p0;
    pyaw += yaw - y0;
}

/* World.rayTraceBlocks over the client world, for orientCamera */
static int tp_ray(void *ctx, const double s[3], const double e[3], double hit[3])
{
    (void)ctx;
    struct rt_mop m;
    if (!raytrace_blocks(CW, s[0], s[1], s[2], e[0], e[1], e[2], 0, 0, 0, &m) || !m.hit) return 0;
    hit[0] = m.hx;
    hit[1] = m.hy;
    hit[2] = m.hz;
    return 1;
}

/* renderstate_compute through a memo of its last input: a frame computes
 * it again from the same bytes (pv_frame, then shot_passes; build_rs
 * clears the padding). Every call here comes through it, so the one
 * static renderstate_compute sets (the provider, from in->dim) is the
 * memo's. */
static void rs_compute(const struct rs_in *in, struct rs_out *o)
{
    static struct rs_in memo_in;
    static struct rs_out memo_out;
    static int memo_ok;
    if (memo_ok && !memcmp(in, &memo_in, sizeof *in)) { *o = memo_out; return; }
    renderstate_compute(in, o);
    memo_in = *in;
    memo_out = *o;
    memo_ok = 1;
}

static void build_rs(struct rs_in *in, float pt)
{
    memset(in, 0, sizeof *in);
    in->rd = render_rd(); in->bob = play_bob; in->clouds = play_clouds;
    in->fov = opt_fov * 40.0F + 70.0F; in->gamma = opt_gamma; in->dw = W; in->dh = H;
    in->fc1 = fc1; in->fc2 = fc2; in->fmh = fmh; in->fmhp = fmhp; in->ruc = ruc;
    in->tfx = play_flicker.x; in->tfy = play_flicker.y;
    in->zoom = 1.0;
    in->ppx = CP.e.prev_pos_x; in->ppy = CP.e.prev_pos_y; in->ppz = CP.e.prev_pos_z;
    in->px = CP.e.pos_x; in->py = CP.e.pos_y; in->pz = CP.e.pos_z;
    in->yaw = yaw; in->pyaw = pyaw; in->pit = pitch; in->ppit = ppitch;
    in->yoff = CP.e.y_offset;
    in->hp = CP.sv.health;
    /* hurtCameraEffect (attackedAtYaw stays 0 on the client), the portal
     * warp, the flash of lastLightningBolt */
    in->hurt = CP.sv.hurt_time;
    in->mhurt = CP.sv.max_hurt_time > 0 ? CP.sv.max_hurt_time : 10;
    in->death = CP.sv.death_time;
    in->portal = CP.portal.time; in->pportal = CP.portal.prev;
    in->conf = potion_map_get(&CP.sv.potions, 9) != NULL;
    in->lbolt = live_weather.last_bolt;
    live_pt = pt;

    /* Entity.getBrightnessForRender: the light at 0.66 of the box's height */
    int bx = (int)floor(CP.e.pos_x), bz = (int)floor(CP.e.pos_z);
    int by = (int)floor(CP.e.pos_y - (double)CP.e.y_offset + 1.8 * 0.66);
    in->brf = world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20 |
              world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4;

    const struct servertick *st = view_st();
    in->wt = CP.cw_day;   /* WorldClient's own clock */
    /* WorldClient's rain and thunder: WorldServer.updateWeather sends each
     * changed strength (S2B 7 and 8) and the client's setRainStrength and
     * setThunderStrength set the previous and current values alike, a tick
     * later: the server's before this tick */
    in->rain = in->prain = st->prev_raining_strength;
    in->thu = in->pthu = st->prev_thundering_strength;
    in->cloud = 16777215;
    /* the provider: the Nether has no void particles; doesXZShowFog is
     * WorldProviderHell's and WorldProviderEnd's (true), so both draw the
     * near fog */
    in->dim = view_dim;
    in->voidp = view_dim == 0; in->voidf = 0.03125;
    in->xzfog = view_dim == -1 || view_dim == 1;
    in->temp = rb_biome_temperature(raster_live_mesher(RL), bx, (int)floor(CP.e.pos_y), bz);
    in->skytemp = sky_color_by_temp(in->temp);
    memcpy(in->lbt, lbt, sizeof lbt);
    in->pt = pt;

    /* GameSettings.thirdPersonView, the sleeping player's bed */
    in->tpv = CP.opt_tpv;
    in->sleep = CP.sv.sleeping;
    in->bedrot = -1;
    if (in->sleep)
    {
        int sx = (int)floor(CP.e.pos_x), sy = (int)floor(CP.e.pos_y), sz = (int)floor(CP.e.pos_z);
        if ((world_get_block(CW, sx, sy, sz) & 4095) == 26) in->bedrot = world_get_meta(CW, sx, sy, sz) & 3;
    }

    /* ActiveRenderInfo.getBlockAtEntityViewpoint, at the interpolated eye */
    double ex = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt;
    double ey = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt;
    double ez = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt;
    if (in->tpv > 0 && !in->sleep)
    {
        /* orientCamera's eight rays from the eye (yOffset - 1.62 is 0 for
         * the standing player), over the client world */
        double eye_y = ey - (double)(in->yoff - 1.62f);
        in->tpdist = renderstate_third_person_distance(ex, eye_y, ez, CP.rotation_yaw, CP.rotation_pitch,
                                                       in->tpv, tp_ray, NULL);
        /* the viewpoint is the camera, back along the look */
        float cy = CP.rotation_yaw, cp = CP.rotation_pitch + (in->tpv == 2 ? 180.0f : 0.0f);
        double lx = (double)(-mh_sin(cy / 180.0f * 3.1415927f) * mh_cos(cp / 180.0f * 3.1415927f));
        double lz = (double)(mh_cos(cy / 180.0f * 3.1415927f) * mh_cos(cp / 180.0f * 3.1415927f));
        double ly = (double)(-mh_sin(cp / 180.0f * 3.1415927f));
        ex -= lx * in->tpdist;
        ey -= ly * in->tpdist;
        ez -= lz * in->tpdist;
    }
    int vx = (int)floor(ex), vy = (int)floor(ey), vz = (int)floor(ez);
    int id = world_get_block(CW, vx, vy, vz);
    if (id >= 8 && id <= 11)
    {
        int meta = world_get_meta(CW, vx, vy, vz);
        float h = (float)(meta >= 8 ? 0 : meta + 1) / 9.0F - 0.11111111F;
        if (ey >= (double)((float)(vy + 1) - h)) id = world_get_block(CW, vx, vy + 1, vz);
    }
    in->mat = id == 8 || id == 9 ? 1 : id == 10 || id == 11 ? 2 : 0;
}

/* EntityRenderer.renderHand: the arm or held item (live_worldfx draws the
 * overlays after it). */
static const char *hand_assets;
static void draw_hand(unsigned char *rgb, const struct rs_out *o, float pt)
{
    static unsigned char *skin, *items, *blocks, *chest;
    static int loaded, items_w = 256, items_h = 256, blocks_w, blocks_h;
    if (!loaded)
    {
        loaded = 1;
        skin = raster_hand_read_rgba(hand_assets, "state/skin.rgba", "mobs/steve.rgba", 64 * 32 * 4);
        char path[1200];
        snprintf(path, sizeof path, "%s/state/gui.json", hud_assets);
        FILE *f = fopen(path, "rb");
        if (f)
        {
            char *line = NULL; size_t cap = 0;
            if (getline(&line, &cap, f) > 0)
            {
                struct jval *g = json_parse(line);
                int64_t v;
                if (json_int(json_get(json_get(g, "items"), "w"), &v)) items_w = (int)v;
                if (json_int(json_get(json_get(g, "items"), "h"), &v)) items_h = (int)v;
                json_free(g);
            }
            else free(line);
            fclose(f);
        }
        items = raster_hand_read_rgba(hud_assets, "state/gui_items.rgba", NULL, (size_t)items_w * items_h * 4);
        snprintf(path, sizeof path, "%s/atlas.json", hand_assets);
        f = fopen(path, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *a = malloc((size_t)n + 1);
            if (a && fread(a, 1, (size_t)n, f) == (size_t)n)
            {
                a[n] = 0;
                struct jval *j = json_parse(a);
                int64_t v;
                if (json_int(json_get(j, "atlas_width"), &v)) blocks_w = (int)v;
                if (json_int(json_get(j, "atlas_height"), &v)) blocks_h = (int)v;
                json_free(j);
            }
            else free(a);
            fclose(f);
        }
        if (blocks_w > 0 && blocks_h > 0)
            blocks = raster_hand_read_rgba(hand_assets, "atlas.rgba", NULL, (size_t)blocks_w * blocks_h * 4);
        chest = raster_hand_read_rgba(hud_assets, "state/gui_chest.rgba", NULL, 64 * 64 * 4);
    }
    struct rs_in in;
    build_rs(&in, pt);
    in.hurt = CP.sv.hurt_time;
    in.mhurt = CP.sv.max_hurt_time > 0 ? CP.sv.max_hurt_time : 10;
    in.death = CP.sv.death_time;
    float proj[16], mv[16];
    renderstate_hand_camera(&in, proj, mv);
    struct raster_hand_in h;
    memset(&h, 0, sizeof h);
    h.pt = pt;
    h.pitch = pitch; h.prev_pitch = ppitch; h.yaw = yaw; h.prev_yaw = pyaw;
    h.arm_pitch = CP.arm_pitch; h.prev_arm_pitch = CP.prev_arm_pitch;
    h.arm_yaw = CP.arm_yaw; h.prev_arm_yaw = CP.prev_arm_yaw;
    h.equipped = equip.eq; h.prev_equipped = equip.peq;
    h.swing = CP.swing_progress; h.prev_swing = CP.prev_swing_progress;
    int bx = (int)floor(CP.e.pos_x), by = (int)floor(CP.e.pos_y), bz = (int)floor(CP.e.pos_z);
    h.light = world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20 |
              world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4;
    if (equip.present)
    {
        const struct surv_stack *cur = &CP.sv.inv[equip.slot >= 0 ? equip.slot : CP.hotbar];
        int same = cur->count > 0 && cur->gen == equip.gen;
        h.has_item = raster_hand_item_lookup(hud_assets, same ? cur->item : equip.item,
                                             same ? cur->damage : equip.damage, &h.item);
        /* ItemFireworkCharge.getColorFromItemStack(stack, 1): the overlay
         * takes the stack's own Explosion colours */
        int tag = same ? cur->tag : equip.tag;
        if (h.has_item && h.item.id == 402 && h.item.npass == 2 && tag) h.item.pass[1].tint = firework_charge_color(tag);
        if (h.has_item && h.item.grass && raster_live_mesher(RL)->tab->grass_map)
            h.item.render_color = (int)(raster_live_mesher(RL)->tab->grass_map[127 << 8 | 127] & 0xffffff);
        if (CP.sv.using_slot >= 0 && CP.sv.using_slot == CP.hotbar) h.use_count = CP.sv.using_count;
        /* EntityPlayer.getItemIcon: a bow in use draws its pulling icon */
        if (h.has_item && h.item.id == 261 && h.use_count > 0)
        {
            int drawn = 72000 - h.use_count;
            const char *icon = drawn >= 18 ? "bow_pulling_2" : drawn > 13 ? "bow_pulling_1" : drawn > 0 ? "bow_pulling_0" : NULL;
            float uv[4];
            if (icon && raster_hud_item_sprite(hud_assets, icon, uv))
            {
                h.item.pass[0].min_u = uv[0]; h.item.pass[0].max_u = uv[1];
                h.item.pass[0].min_v = uv[2]; h.item.pass[0].max_v = uv[3];
            }
        }
        /* ItemStack.hasEffect(0): the item's own foil or an enchantment */
        if (h.has_item)
        {
            struct equip_slot es = {0};
            es.id = same ? cur->item : equip.item;
            es.damage = same ? cur->damage : equip.damage;
            es.count = 1;
            es.tag = same ? cur->tag : 0;
            struct thing_item ti;
            equip_item(&es, &ti);
            h.item.eff = ti.eff;
        }
    }
    static unsigned char *glint;
    static int glint_loaded;
    if (!glint_loaded)
    {
        glint_loaded = 1;
        glint = raster_hand_read_rgba(hud_assets, "state/gui_glint.rgba", "gui_glint.rgba", 64 * 64 * 4);
    }
    h.glint = glint;
    h.now = frame_now_ms;
    h.proj = proj; h.mv = mv;
    h.lm = (const uint32_t *)o->lm;
    h.scene = hand_assets;
    if (play_anim_ok) {
        if (items) texanim_apply(&play_anim, &play_anim_st, TEXANIM_ITEMS, items, items_w, items_h);
        if (blocks) texanim_apply(&play_anim, &play_anim_st, TEXANIM_BLOCKS, blocks, blocks_w, blocks_h);
    }
    h.skin = skin; h.items = items; h.items_w = items_w; h.items_h = items_h;
    h.blocks = blocks; h.blocks_w = blocks_w; h.blocks_h = blocks_h; h.chest = chest;
    raster_hand_draw(&h, rgb, W, H);
}

/* F3+B's hitboxes. The mob and item models are not ported yet, so every
 * entity the server holds is drawn as its bounding box at the interpolated
 * position, depth tested against the frame: red hostile, white passive,
 * yellow items and orbs. */
static void box_line(unsigned char *rgb, const float *depth, const float a0[4], const float b0[4],
                     const unsigned char col[3])
{
    float a[4], b[4];
    memcpy(a, a0, sizeof a); memcpy(b, b0, sizeof b);
    float da = a[2] + a[3], db = b[2] + b[3];   /* the near plane: z >= -w */
    if (da < 0 && db < 0) return;
    if (da < 0 || db < 0)
    {
        float t = da / (da - db);
        float *p = da < 0 ? a : b;
        for (int i = 0; i < 4; ++i) p[i] = a[i] + t * (b[i] - a[i]);
    }
    if (a[3] <= 1e-6F || b[3] <= 1e-6F) return;
    float ax = (a[0] / a[3] * 0.5F + 0.5F) * W, ay = (0.5F - a[1] / a[3] * 0.5F) * H, az = a[2] / a[3] * 0.5F + 0.5F;
    float bx = (b[0] / b[3] * 0.5F + 0.5F) * W, by = (0.5F - b[1] / b[3] * 0.5F) * H, bz = b[2] / b[3] * 0.5F + 0.5F;
    float dx = bx - ax, dy = by - ay;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (steps > 4 * (W + H)) return;
    for (int i = 0; i <= steps; ++i)
    {
        float t = steps ? (float)i / steps : 0;
        int x = (int)floorf(ax + t * dx), y = (int)floorf(ay + t * dy);
        if (x < 0 || x >= W || y < 0 || y >= H) continue;
        size_t k = (size_t)y * W + x;
        if (az + t * (bz - az) > depth[k] + 2e-5F) continue;
        memcpy(rgb + k * 3, col, 3);
    }
}

static void draw_box(unsigned char *rgb, const float *depth, const struct rs_out *o, struct aabb bb,
                     double ox, double oy, double oz, const unsigned char col[3])
{
    float c[8][4];
    for (int i = 0; i < 8; ++i)
    {
        float p[4] = {(float)((i & 1 ? bb.max_x : bb.min_x) + ox - o->camx),
                      (float)((i & 2 ? bb.max_y : bb.min_y) + oy - o->camy),
                      (float)((i & 4 ? bb.max_z : bb.min_z) + oz - o->camz), 1};
        float e[4];
        for (int r = 0; r < 4; ++r) e[r] = o->mv[r] * p[0] + o->mv[4+r] * p[1] + o->mv[8+r] * p[2] + o->mv[12+r];
        for (int r = 0; r < 4; ++r) c[i][r] = o->proj[r] * e[0] + o->proj[4+r] * e[1] + o->proj[8+r] * e[2] + o->proj[12+r] * e[3];
    }
    static const int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (int i = 0; i < 12; ++i) box_line(rgb, depth, c[edges[i][0]], c[edges[i][1]], col);
}

static int hostile(int kind)
{
    return kind == SK_SLIME || kind == SK_MAGMA_CUBE || (kind >= HK_ZOMBIE && kind < HK_KINDS &&
           kind != HK_PLAYER && kind != AK_SQUID && kind != AK_BAT);
}

/* Entity.getBrightness: World.getLightBrightness over getBlockLightValue,
 * the sky less WorldClient's skylightSubtracted, none at all under a
 * hasNoSky provider (the Nether, the End), against the block light */
static float ent_brightness(int brightness)
{
    int sky = view_dim == 0 ? ((brightness >> 20) & 15) - client_skylight_subtracted() : 0;
    int blk = (brightness >> 4) & 15;
    int l = sky > blk ? sky : blk;
    return lbt[l < 0 ? 0 : l > 15 ? 15 : l];
}

/* EntityBlaze and EntityMagmaCube: getBrightnessForRender 15728880 and
 * getBrightness 1, whatever the light where they stand */
static void mob_self_lit(struct mob_render_input *m)
{
    if (m->kind != MOB_BLAZE && m->kind != MOB_MAGMA_CUBE) return;
    m->brightness = 15728880;
    m->brightness_scalar = 1.0f;
}

static float mob_yaw(float from, float to, float pt)
{
    float delta = to - from;
    while (delta < -180.0f) delta += 360.0f;
    while (delta >= 180.0f) delta -= 360.0f;
    return from + pt * delta;
}

static int stage_kind = -1; /* --shot-model: 0 dragon, 1 ender crystal */


/* the frame judge's second pass: the frame without the dropped items and orbs */
static int shot_skip_drops;

/* ---------------------------------------------------------- the things */

/* The non-living entities RenderGlobal.renderEntities draws with their own
 * Render classes (raster_things.c): WorldClient.weatherEffects' bolts, then
 * the arrows, fireballs, thrown sprites, primed TNT and falling blocks, as
 * EntityRenderProbe lists them, from the server's entities of the viewed
 * dimension. */
struct things_buf { char *b; size_t n, cap; };

static void tb_add(struct things_buf *t, const char *fmt, ...)
{
    va_list ap;
    for (;;)
    {
        va_start(ap, fmt);
        int k = vsnprintf(t->b + t->n, t->cap - t->n, fmt, ap);
        va_end(ap);
        if (k >= 0 && (size_t)k < t->cap - t->n) { t->n += (size_t)k; return; }
        t->cap = t->cap * 2 + 4096;
        t->b = realloc(t->b, t->cap);
        if (!t->b) exit(1);
    }
}

static uint32_t tb_f(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static unsigned long long tb_d(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

/* Entity.getBrightnessForRender and getBrightness at 0.66 of the box */
static void thing_light(const struct entity *e, int *brf, float *bright)
{
    int bx = (int)floor(e->pos_x), bz = (int)floor(e->pos_z);
    int by = (int)floor(e->bounding_box.min_y + (e->bounding_box.max_y - e->bounding_box.min_y) * 0.66);
    struct world *w = view_world();
    /* a copy the client still draws where the server has let its chunk go
     * reads the client's cells: the server world's read would load, and
     * generate, the chunk (the frame moved the simulation: explore-long-s1
     * row 10322 in the pipeline) */
    if (w != CW && CW && !world_chunk_loaded(w, bx >> 4, bz >> 4)) w = CW;
    *brf = world_get_light(w, LIGHT_SKY, bx, by, bz) << 20 | world_get_light(w, LIGHT_BLOCK, bx, by, bz) << 4;
    int l = world_get_full_block_light_value(w, bx, by, bz, client_skylight_subtracted());
    *bright = lbt[l < 0 ? 0 : l > 15 ? 15 : l];
}

/* Entity.isInRangeToRender3d: the box's average edge, times 64 and the
 * class's weight, from the camera */
static int thing_in_range(const struct entity *e, double weight, const double cam[3])
{
    const struct aabb *b = &e->bounding_box;
    double edge = ((b->max_x - b->min_x) + (b->max_y - b->min_y) + (b->max_z - b->min_z)) / 3.0;
    double r = edge * 64.0 * weight;
    double dx = e->pos_x - cam[0], dy = e->pos_y - cam[1], dz = e->pos_z - cam[2];
    return dx * dx + dy * dy + dz * dz < r * r;
}

static void thing_base(struct things_buf *t, const char *k, const struct entity *e, double x, double y, double z,
                       float yaw, float pyaw, float pitch, float ppitch, int vis)
{
    int brf, burning = e->fire > 0;
    float bright;
    thing_light(e, &brf, &bright);
    if (burning) brf = 15728880;
    tb_add(t, "%s{\"k\":\"%s\",\"vis\":%d,\"x\":\"d:%016llx\",\"y\":\"d:%016llx\",\"z\":\"d:%016llx\","
              "\"yaw\":\"f:%08x\",\"pyaw\":\"f:%08x\",\"pitch\":\"f:%08x\",\"ppitch\":\"f:%08x\","
              "\"brf\":%d,\"bright\":\"f:%08x\",\"burning\":%d,\"width\":\"f:%08x\",\"height\":\"f:%08x\",\"fdy\":\"f:%08x\"",
           t->n > 1 ? "," : "", k, vis, tb_d(x), tb_d(y), tb_d(z), tb_f(yaw), tb_f(pyaw), tb_f(pitch), tb_f(ppitch),
           brf, tb_f(bright), burning, tb_f(e->width), tb_f(e->height), tb_f((float)(e->pos_y - e->bounding_box.min_y)));
}

static void thing_icon(struct things_buf *t, const char *key, int item, int damage)
{
    const struct jval *ic = raster_hud_item_entry(hud_assets, item, damage);
    if (!ic) return;
    static const char *uv[4] = {"minU", "maxU", "minV", "maxV"};
    if (key) tb_add(t, ",\"%s\":{", key);
    for (int i = 0; i < 4; ++i)
    {
        const char *v = json_str(json_get(ic, uv[i]));
        if (v) tb_add(t, "%s\"%s\":\"%s\"", key && i == 0 ? "" : ",", uv[i], v);
    }
    if (key) tb_add(t, "}");
}

static void draw_things(const struct entity_raster_target *target, float pt, const char *assets)
{
    struct things_buf t = {malloc(4096), 0, 4096};
    tb_add(&t, "[");
    /* the weather effects first */
    for (int i = 0; i < live_weather.n; ++i)
    {
        const struct cs_bolt *b = &live_weather.b[i];
        int q = b->id % CS_BOLTS;
        if (live_bolt_pos[q].id != b->id) continue;
        tb_add(&t, "%s{\"k\":\"bolt\",\"vis\":1,\"x\":\"d:%016llx\",\"y\":\"d:%016llx\",\"z\":\"d:%016llx\","
                   "\"brf\":15728880,\"bolt\":\"%lld\"}", t.n > 1 ? "," : "",
               tb_d(live_bolt_pos[q].x), tb_d(live_bolt_pos[q].y), tb_d(live_bolt_pos[q].z), (long long)b->vertex);
    }
    double cam[3] = {target->cam[0], target->cam[1], target->cam[2]};
    for (int l = 0; l < 2; ++l)
    {
        const ie_world *iw = drop_lists(l);
        for (int i = 0; iw && i < iw->n; ++i)
        {
            const ie_ent *it = ie_ent_at(iw->slot[i]);
            if (!it || it->is_dead || it->kind == IE_ITEM || it->kind == IE_ORB) continue;
            /* the fireballs are WorldClient's copies, below */
            if (SR.combat && (it->kind == IE_LARGE_FIREBALL || it->kind == IE_SMALL_FIREBALL)) continue;
            const struct entity *e = &it->e;
            int fresh = it->first_update || it->ticks_existed == 0;
            double x = fresh ? e->pos_x : it->prev_x + (e->pos_x - it->prev_x) * pt;
            double y = fresh ? e->pos_y : it->prev_y + (e->pos_y - it->prev_y) * pt;
            double z = fresh ? e->pos_z : it->prev_z + (e->pos_z - it->prev_z) * pt;
            const char *k = it->kind == IE_ARROW ? "arrow" : it->kind == IE_LARGE_FIREBALL ? "fireball" :
                            it->kind == IE_SMALL_FIREBALL ? "smallfireball" : it->kind == IE_TNT ? "tnt" : "sprite";
            /* EntityArrow's renderDistanceWeight 10; a fireball's and a
             * throwable's isInRangeToRenderDist take the edge four times */
            double weight = it->kind == IE_ARROW ? 10.0 : it->kind == IE_TNT ? 1.0 : 4.0;
            thing_base(&t, k, e, x, y, z, it->rotation_yaw, it->prev_yaw, it->rotation_pitch, it->prev_pitch,
                       thing_in_range(e, weight, cam));
            if (it->kind == IE_ARROW) tb_add(&t, ",\"shake\":%d", it->shake);
            else if (it->kind == IE_LARGE_FIREBALL || it->kind == IE_SMALL_FIREBALL) thing_icon(&t, NULL, 385, 0);
            else if (it->kind == IE_TNT) tb_add(&t, ",\"fuse\":%d", it->fuse);
            else
            {
                int item = it->kind == IE_SNOWBALL ? 332 : it->kind == IE_EGG ? 344 : it->kind == IE_ENDER_PEARL ? 368 :
                           it->kind == IE_EXP_BOTTLE ? 384 : it->kind == IE_ENDER_EYE ? 381 : 373;
                if (item == 373)
                {
                    /* RenderSnowball's splash bottle and its tinted overlay */
                    const struct jval *pe = raster_hud_item_entry(hud_assets, 373, 16384);
                    const struct jval *p1 = pe ? json_get(pe, "pass1") : NULL;
                    static const char *uv[4] = {"minU", "maxU", "minV", "maxV"};
                    for (int q = 0; p1 && q < 4; ++q) tb_add(&t, ",\"%s\":\"%s\"", uv[q], json_str(json_get(p1, uv[q])));
                    thing_icon(&t, "overlay", 373, 16384);
                    const struct jval *pc = raster_hud_item_entry(hud_assets, 373, it->potion_damage);
                    int64_t tint = 0x385dc6;
                    if (pc) json_int(json_get(pc, "tint"), &tint);
                    tb_add(&t, ",\"color\":%lld", (long long)tint);
                }
                else thing_icon(&t, NULL, item, 0);
            }
            tb_add(&t, "}");
        }
    }
    /* WorldClient's fireball copies (clientworld.c: the S0E's quantized
     * position and motion, then the copy's own update adding the
     * acceleration), which RenderFireball draws from lastTickPos (the copy's
     * prevPos) to the position; burning once its update's setFire(1) ran or
     * the server's flag reached it */
    for (int i = 0; SR.combat && i < SR.combat->client.nents; ++i)
    {
        const struct client_entity *ce = &SR.combat->client.ents[i];
        if (ce->is_dead || !ce->is_fireball) continue;
        struct entity fe;
        memset(&fe, 0, sizeof fe);
        fe.pos_x = ce->x;
        fe.pos_y = ce->y;
        fe.pos_z = ce->z;
        fe.width = ce->width;
        fe.height = ce->height;
        double half = (double)(ce->width / 2.0F);
        fe.bounding_box = aabb_make(ce->x - half, ce->y, ce->z - half, ce->x + half, ce->y + (double)ce->height, ce->z + half);
        fe.fire = ce->fireball_burning || (ce->flags0 & 1) ? 20 : 0;
        double x = ce->prev_x + (ce->x - ce->prev_x) * pt;
        double y = ce->prev_y + (ce->y - ce->prev_y) * pt;
        double z = ce->prev_z + (ce->z - ce->prev_z) * pt;
        thing_base(&t, ce->is_large_fireball ? "fireball" : "smallfireball", &fe, x, y, z, ce->yaw, ce->prev_yaw,
                   ce->pitch, ce->prev_pitch, thing_in_range(&fe, 4.0, cam));
        thing_icon(&t, NULL, 385, 0);
        tb_add(&t, "}");
    }
    /* WorldClient's EntityFallingBlock copies (pickobj.c: the S0E's spawn,
     * the tracker's moves and each copy's own onUpdate), drawn where the
     * client has them: lastTickPos (the copy's prevPos, which its update
     * sets first) to the position */
    int nfo = 0;
    const struct pickobj *fobjs = CP.pickobj ? pickobj_client_objs(&CP, &nfo) : NULL;
    for (int i = 0; i < nfo; ++i)
    {
        const struct pickobj *f = &fobjs[i];
        if (f->dead || f->kind != PK_FALLING) continue;
        const struct entity *e = &f->e;
        double x = e->prev_pos_x + (e->pos_x - e->prev_pos_x) * pt;
        double y = e->prev_pos_y + (e->pos_y - e->prev_pos_y) * pt;
        double z = e->prev_pos_z + (e->pos_z - e->prev_pos_z) * pt;
        thing_base(&t, "falling", e, x, y, z, f->yaw, f->yaw, f->pitch, f->pitch, thing_in_range(e, 1.0, cam));
        int bx = (int)floor(e->pos_x), by = (int)floor(e->pos_y), bz = (int)floor(e->pos_z);
        struct world *w = CW;
        /* Block.getBlockBrightness: the lightmap coordinate of the cell */
        int bbr = world_get_light(w, LIGHT_SKY, bx, by, bz) << 20 | world_get_light(w, LIGHT_BLOCK, bx, by, bz) << 4;
        tb_add(&t, ",\"block\":%d,\"meta\":%d,\"draws\":%d,\"bbr\":%d", f->block, f->meta,
               (world_get_block(w, bx, by, bz) & 4095) != f->block, bbr);
        if (f->block == 145 || f->block == 122)
        {
            /* the anvil's and the dragon egg's RenderBlocks paths over the
             * world at the cell: each quad's corners, uvs, colour and
             * brightness */
            static struct rb_tess ft;
            if (!ft.raw && rb_tess_init(&ft, 8 * 4 * 64) != 0) exit(1);
            rb_mesh_falling_block(raster_live_mesher(RL), &ft, f->block, f->meta, bx, by, bz);
            tb_add(&t, ",\"quads\":[");
            for (int v = 0; v < ft.n / 8; ++v)
            {
                const int32_t *r = ft.raw + (size_t)v * 8;
                tb_add(&t, "%s\"f:%08x\",\"f:%08x\",\"f:%08x\",\"f:%08x\",\"f:%08x\",%d,%d", v ? "," : "",
                       (unsigned)r[0], (unsigned)r[1], (unsigned)r[2], (unsigned)r[3], (unsigned)r[4], r[5], r[7]);
            }
            tb_add(&t, "]");
        }
        tb_add(&t, "}");
    }
    tb_add(&t, "]");
    if (t.n > 2)
    {
        struct jval *ents = json_parse(t.b);   /* owns the text */
        if (ents)
        {
            raster_things_live(assets, target, ents, pyaw + (yaw - pyaw) * pt, ppitch + (pitch - ppitch) * pt, pt,
                               frame_now_ms);
            json_free(ents);
            return;
        }
    }
    free(t.b);
}

/* RendererLivingEntity's equipment as EntityRenderProbe writes it (the held
 * item and the armour, RenderBiped): the icon passes from the item table,
 * isFull3D, the fishing rod's rotation, the multi-pass items, the bow, the
 * glint, ItemArmor.renderIndex and a leather piece's colour. */
/* equip_item's answers for untagged stacks, by id and damage: the item
 * table and the icon entries it reads never change once loaded */
static struct { int id, damage, used; struct thing_item it; } equip_memo[64];

static void equip_item(const struct equip_slot *sl, struct thing_item *out)
{
    memset(out, 0, sizeof *out);
    if (sl->id <= 0 || sl->count <= 0) return;
    unsigned mk = ((unsigned)sl->id * 31u + (unsigned)sl->damage) & 63u;
    if (sl->tag <= 0 && equip_memo[mk].used && equip_memo[mk].id == sl->id && equip_memo[mk].damage == sl->damage)
    {
        *out = equip_memo[mk].it;
        return;
    }
    const struct jval *ic = raster_hud_item_entry(hud_assets, sl->id, sl->damage);
    if (!ic) return;
    int id = sl->id;
    int64_t mode = 0, eff = 0, tint = 16777215;
    json_int(json_get(ic, "mode"), &mode);
    json_int(json_get(ic, "effect"), &eff);
    json_int(json_get(ic, "tint"), &tint);
    const char *cls = ITEMS[id].class_name ? ITEMS[id].class_name : "";
    int full3d = ITEMS[id].kind == ITEM_TOOL || ITEMS[id].kind == ITEM_SWORD || !strcmp(cls, "ItemHoe") ||
                 id == 280 || id == 352 || id == 369 || id == 346 || id == 398;
    int ari = id >= 298 && id <= 317 ? (id - 298) / 4 : -1;
    int cloth = id >= 298 && id <= 301;
    int color = 10511680;
    const struct itag *tg = itag_get(sl->tag);
    if (cloth && tg && tg->has_color) color = tg->color;
    if (sl->tag && tg && tg->nench > 0) eff = 1;
    struct things_buf t = {malloc(1024), 0, 1024};
    static const char *uv[4] = {"minU", "maxU", "minV", "maxV"};
    /* RenderBlocks.renderItemIn3d of the block's render type: the icon
     * entry's mode 1 (RenderItem's own test) */
    int in3d = id < 256 && mode == 1;
    tb_add(&t, "{\"id\":%d,\"dmg\":%d,\"sprite\":%d,\"full3d\":%d,\"rot\":%d,\"multi\":%d,\"bow\":%d,\"eff\":%d,"
               "\"b3d\":%d,\"ib3d\":%d,\"passes\":[",
           id, sl->damage, id < 256 ? 0 : 1, full3d, id == 346 || id == 398, mode == 2, id == 261, (int)eff, in3d, in3d);
    const struct jval *pass[2] = {ic, mode == 2 ? json_get(ic, "pass1") : NULL};
    for (int k = 0; k < 2 && pass[k]; ++k)
    {
        int64_t pt_tint = 16777215;
        json_int(json_get(pass[k], "tint"), &pt_tint);
        if (k == 0 && cloth) pt_tint = color;
        if (k == 1 && id == 402 && sl->tag) pt_tint = firework_charge_color(sl->tag);
        tb_add(&t, "%s{\"tint\":%lld", k ? "," : "", (long long)pt_tint);
        for (int q = 0; q < 4; ++q)
        {
            const char *v = json_str(json_get(pass[k], uv[q]));
            if (v) tb_add(&t, ",\"%s\":\"%s\"", uv[q], v);
        }
        tb_add(&t, ",\"iw\":16,\"ih\":16}");
    }
    tb_add(&t, "]");
    if (ari >= 0) tb_add(&t, ",\"ari\":%d,\"cloth\":%d", ari, cloth);
    if (cloth) tb_add(&t, ",\"color\":%d", color);
    tb_add(&t, "}");
    struct jval *j = json_parse(t.b);
    if (!j) { free(t.b); return; }
    thing_item_parse(j, out);
    json_free(j);
    (void)tint;
    if (sl->tag <= 0)
    {
        equip_memo[mk].id = sl->id;
        equip_memo[mk].damage = sl->damage;
        equip_memo[mk].it = *out;
        equip_memo[mk].used = 1;
    }
}

/* Frustrum.isBoundingBoxInFrustum (ClippingHelper.isBoxInFrustum): out when
 * all eight corners lie outside one of the six planes of the frame's
 * projection and modelview, the box taken relative to the render origin */
static int box_in_frustum(const struct rs_out *o, const struct aabb *b)
{
    int out[6] = {0};
    for (int i = 0; i < 8; ++i)
    {
        float pos[4] = {(float)((i & 1 ? b->max_x : b->min_x) - o->camx),
                        (float)((i & 2 ? b->max_y : b->min_y) - o->camy),
                        (float)((i & 4 ? b->max_z : b->min_z) - o->camz), 1};
        float eye[4], clip[4];
        for (int r = 0; r < 4; ++r)
            eye[r] = o->mv[r] * pos[0] + o->mv[4 + r] * pos[1] + o->mv[8 + r] * pos[2] + o->mv[12 + r];
        for (int r = 0; r < 4; ++r)
            clip[r] = o->proj[r] * eye[0] + o->proj[4 + r] * eye[1] + o->proj[8 + r] * eye[2] + o->proj[12 + r] * eye[3];
        for (int plane = 0; plane < 6; ++plane)
        {
            float sign = plane & 1 ? -1.0f : 1.0f;
            if (clip[3] - sign * clip[plane / 2] < 0.0f) ++out[plane];
        }
    }
    for (int plane = 0; plane < 6; ++plane) if (out[plane] == 8) return 0;
    return 1;
}

/* RenderPlayer.doRender's inputs for the client player: the position at the
 * feet (y - yOffset; EntityPlayerSP takes no sneak drop), the turned body and
 * head, the limbs, ModelBiped's flags from the held item and its use, the
 * armour, the bed. */
static void live_player_render(struct mob_render_input *m, float pt)
{
    memset(m, 0, sizeof *m);
    m->kind = MOB_PLAYER;
    m->width = CP.e.width;
    m->height = CP.e.height;
    m->y_offset = CP.e.y_offset;
    m->x = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt;
    m->y = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt - (double)CP.e.y_offset;
    m->z = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt;
    m->body_yaw = mob_yaw(CP.prev_render_yaw_offset, CP.render_yaw_offset, pt);
    m->head_yaw = mob_yaw(CP.prev_yaw_head, CP.yaw_head, pt);
    m->pitch = ppitch + (pitch - ppitch) * pt;
    m->limb = CP.limb_swing - CP.limb_swing_amount * (1.0f - pt);
    m->limb_amount = CP.prev_limb_swing_amount + (CP.limb_swing_amount - CP.prev_limb_swing_amount) * pt;
    float sw = CP.swing_progress - CP.prev_swing_progress;
    if (sw < 0.0f) sw += 1.0f;
    m->swing = CP.prev_swing_progress + sw * pt;
    m->age = CP.ticks_existed;
    m->partial_tick = pt;
    m->hurt = CP.sv.hurt_time;
    m->death = CP.sv.death_time;
    m->burning = CP.e.fire > 0 || CP.sv.fire > 0;
    m->sneak = CP.in_sneak && !CP.sv.sleeping;
    m->riding = CP.riding_id != 0;
    m->arrows = client_arrows;
    m->entity_id = SR.player_entity_id;
    const struct surv_stack *held = &CP.sv.inv[CP.hotbar];
    struct equip_slot sl = {0};
    if (held->count > 0)
    {
        sl.id = held->item; sl.damage = held->damage; sl.count = held->count; sl.tag = held->tag;
        m->held_right = 1;
    }
    equip_item(&sl, &m->equip[0]);
    for (int k = 0; k < 4; ++k)
    {
        const struct surv_stack *a = &CP.sv.inv[36 + k];
        struct equip_slot as = {0};
        if (a->count > 0) { as.id = a->item; as.damage = a->damage; as.count = a->count; as.tag = a->tag; }
        equip_item(&as, &m->equip[1 + k]);
    }
    /* getItemInUseCount: the sword blocks (3), the bow draws (aimedBow and
     * EntityPlayer.getItemIcon's pulling icons) */
    if (held->count > 0 && CP.sv.using_slot == CP.hotbar && CP.sv.using_count > 0)
    {
        int id = held->item;
        if (ITEMS[id].kind == ITEM_SWORD) m->held_right = 3;
        else if (id == 261)
        {
            m->aimed_bow = 1;
            int drawn = 72000 - CP.sv.using_count;
            const char *icon = drawn >= 18 ? "bow_pulling_2" : drawn > 13 ? "bow_pulling_1" : drawn > 0 ? "bow_pulling_0" : NULL;
            float uv[4];
            if (icon && raster_hud_item_sprite(hud_assets, icon, uv))
            {
                m->equip[0].pass[0].min_u = uv[0]; m->equip[0].pass[0].max_u = uv[1];
                m->equip[0].pass[0].min_v = uv[2]; m->equip[0].pass[0].max_v = uv[3];
            }
        }
    }
    if (CP.sv.sleeping)
    {
        /* rotateCorpse's getBedOrientationInDegrees and renderLivingAt's
         * func_71013_b offset, from the bed's direction */
        int bx = (int)floor(CP.e.pos_x), by = (int)floor(CP.e.pos_y), bz = (int)floor(CP.e.pos_z);
        int dir = (world_get_block(CW, bx, by, bz) & 4095) == 26 ? world_get_meta(CW, bx, by, bz) & 3 : -1;
        static const float deg[4] = {90.0f, 0.0f, 270.0f, 180.0f};
        m->sleeping = 1;
        m->bed_deg = dir >= 0 ? deg[dir] : 0.0f;
        if (dir == 0) m->z += -1.8f;
        else if (dir == 1) m->x += 1.8f;
        else if (dir == 2) m->z += 1.8f;
        else if (dir == 3) m->x += -1.8f;
    }
    int bx = (int)floor(m->x), by = (int)floor(m->y + CP.e.height * 0.66), bz = (int)floor(m->z);
    m->brightness = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
                    (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
    m->brightness_scalar = ent_brightness(m->brightness);
    m->now = frame_now_ms;
}

/* The client world's entities by id for one frame's draw (nothing changes
 * them meanwhile): open addressing over a power of two at least twice the
 * list, each id's first entity, as clientworld_get_entity's scan finds it */
static struct { int32_t id, at; } cwidx[2 * CW_MAX_ENTITIES];
static int cwidx_mask;

static void cwidx_build(const struct clientworld *cw)
{
    int size = 64;
    while (size < 2 * cw->nents) size *= 2;
    cwidx_mask = size - 1;
    for (int i = 0; i < size; ++i) cwidx[i].at = -1;
    for (int i = 0; i < cw->nents; ++i)
    {
        unsigned h = ((unsigned)cw->ents[i].id * 0x9E3779B1u) & (unsigned)cwidx_mask;
        while (cwidx[h].at >= 0 && cwidx[h].id != cw->ents[i].id) h = (h + 1) & (unsigned)cwidx_mask;
        if (cwidx[h].at < 0) cwidx[h].id = cw->ents[i].id, cwidx[h].at = i;
    }
}

static struct client_entity *cwidx_get(struct clientworld *cw, int id)
{
    for (unsigned h = ((unsigned)id * 0x9E3779B1u) & (unsigned)cwidx_mask; cwidx[h].at >= 0;
         h = (h + 1) & (unsigned)cwidx_mask)
        if (cwidx[h].id == id) return &cw->ents[cwidx[h].at];
    return NULL;
}

static int draw_hitboxes(unsigned char *rgb, const struct rs_out *o, float pt,
                         const float *depth, const char *assets)
{
    static const unsigned char red[3] = {255, 70, 70}, white[3] = {255, 255, 255}, yellow[3] = {255, 220, 60};
    if (!depth) depth = raster_live_depth(RL);
    double cam[3] = {o->camx, o->camy, o->camz};
    struct entity_raster_target target = {.w = W, .h = H, .proj = o->proj, .mv = o->mv,
        .cam = cam, .fog = o->gfogc, .fogs = o->gfogs, .foge = o->gfoge, .fogd = o->gfogd,
        .fogm = o->gfogm, .lm = (const uint32_t *)o->lm,
        .depth = raster_live_depth_mut(RL), .rgb = rgb, .material = {1, 1, 1}};
    int n = 0;
    if (stage_kind >= 0)
    {
        /* The dragon and the End crystal have no native entity yet (the dragon
         * runs in dragon.c, the crystal is not in the item lists), so this
         * staged pose is the play path drawing their models: a straight ring
         * (all movement offsets equal) with animTime advancing per frame. */
        static int stage_frame;
        struct mob_render_input m = {0};
        double look[3] = {-(double)o->mv[8], -(double)o->mv[9], -(double)o->mv[10]};
        double d = stage_kind == 0 ? 11.0 : 2.8;
        m.x = o->camx + look[0] * d;
        m.y = o->camy + look[1] * d;
        m.z = o->camz + look[2] * d;
        m.partial_tick = pt;
        int bx = (int)floor(m.x), by = (int)floor(m.y), bz = (int)floor(m.z);
        m.brightness = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
                       (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
        m.brightness_scalar = ent_brightness(m.brightness);
        m.now = frame_now_ms;
        if (stage_kind == 0)
        {
            m.kind = MOB_DRAGON;
            m.height = 8.0f;
            m.width = 16.0f;
            m.anim = (float)stage_frame * 0.05f;
            m.death_ticks = 0;
            m.age = stage_frame;
            for (int i = 0; i < 24; ++i) m.off[i][1] = m.y;
        }
        else
        {
            m.kind = MOB_ENDER_CRYSTAL;
            m.height = 2.0f;
            m.width = 2.0f;
            m.crystal_rot = (float)stage_frame * 1.0f;
        }
        ++stage_frame;
        n += raster_mobs_draw(assets, &target, &m, 1, &RW,
            raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
    }
    /* RenderGlobal.renderEntities draws the view entity itself in third
     * person or asleep: RenderPlayer, first in the client's entity list */
    if ((CP.opt_tpv != 0 || CP.sv.sleeping) && CP.sv.death_time < 20 && box_in_frustum(o, &CP.e.bounding_box))
    {
        struct mob_render_input m;
        live_player_render(&m, pt);
        n += raster_mobs_draw(assets, &target, &m, 1, &RW,
            raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
    }
    if (SR.combat) cwidx_build(&SR.combat->client);
    for (int i = 0; i < SR.d->anw.n; ++i)
    {
        const struct an_ent *a = an_ent_at(SR.d->anw.slot[i]);
        if (!a || !a->used || !a->is_living || !a->livh || lv_get(a->livh) == lv_get(SR.player_livh)) continue;
        const struct entity *e = &lv_get(a->livh)->e;
        const struct living *l = lv_get(a->livh);
        /* WorldClient draws its own copy: the tracker's quantized positions,
         * the three-step interpolation toward each packet, a tick behind the
         * server; the pick (combat_pick_entity) reads the same copy. One the
         * client does not hold is not drawn. */
        const struct client_entity *ce = NULL;
        if (SR.combat)
        {
            ce = cwidx_get(&SR.combat->client, l->entity_id);
            if (!ce || ce->is_dead) continue;
        }
        const struct cent_living *cl = ce ? cent_find(l->entity_id) : NULL;
        if (cl && cl->dead) continue;
        double ex = e->prev_pos_x + (e->pos_x - e->prev_pos_x) * pt;
        double ey = e->prev_pos_y + (e->pos_y - e->prev_pos_y) * pt;
        double ez = e->prev_pos_z + (e->pos_z - e->prev_pos_z) * pt;
        if (ce)
        {
            ex = ce->prev_x + (ce->x - ce->prev_x) * pt;
            ey = ce->prev_y + (ce->y - ce->prev_y) * pt;
            ez = ce->prev_z + (ce->z - ce->prev_z) * pt;
        }
        enum mob_model_kind mk;
        int modeled = 1;
        switch (l->kind) {
        case AK_PIG: mk = MOB_PIG; break;
        case AK_COW: mk = MOB_COW; break;
        case AK_MOOSHROOM: mk = MOB_MOOSHROOM; break;
        case AK_SHEEP: mk = MOB_SHEEP; break;
        case AK_CHICKEN: mk = MOB_CHICKEN; break;
        case HK_ZOMBIE: mk = MOB_ZOMBIE; break;
        case HK_SKELETON: mk = l->skeleton_type == 1 ? MOB_WITHER_SKELETON : MOB_SKELETON; break;
        case HK_CREEPER: mk = MOB_CREEPER; break;
        case HK_SPIDER: mk = MOB_SPIDER; break;
        case HK_CAVE_SPIDER: mk = MOB_CAVE_SPIDER; break;
        case HK_ENDERMAN: mk = MOB_ENDERMAN; break;
        case HK_WITCH: mk = MOB_WITCH; break;
        case SK_SLIME: mk = MOB_SLIME; break;
        case HK_SILVERFISH: mk = MOB_SILVERFISH; break;
        case HK_PIGMAN: mk = MOB_PIGMAN; break;
        case GK_GHAST: mk = MOB_GHAST; break;
        case HK_BLAZE: mk = MOB_BLAZE; break;
        case SK_MAGMA_CUBE: mk = MOB_MAGMA_CUBE; break;
        case VK_VILLAGER: mk = MOB_VILLAGER; break;
        case VK_IRON_GOLEM: mk = MOB_IRON_GOLEM; break;
        case AK_SQUID: mk = MOB_SQUID; break;
        case AK_BAT: mk = MOB_BAT; break;
        default: modeled = 0; mk = MOB_PIG; break;
        }
        if (modeled) {
            struct mob_render_input m = {0};
            m.kind = mk;
            /* RenderManager.playerViewY, which the fire quads turn by */
            m.view_yaw = pyaw + (yaw - pyaw) * pt;
            m.height = e->height;
            m.width = e->width;
            m.x = ex;
            m.y = ey;
            m.z = ez;
            m.body_yaw = mob_yaw(l->prev_render_yaw_offset, l->render_yaw_offset, pt);
            m.head_yaw = mob_yaw(l->prev_rotation_yaw_head, l->rotation_yaw_head, pt);
            m.pitch = l->prev_rotation_pitch + (l->rotation_pitch - l->prev_rotation_pitch) * pt;
            m.limb = l->limb_swing - l->limb_swing_amount * (1.0f - pt);
            m.limb_amount = l->prev_limb_swing_amount +
                (l->limb_swing_amount - l->prev_limb_swing_amount) * pt;
            m.swing = l->prev_swing_progress + (l->swing_progress - l->prev_swing_progress) * pt;
            m.age = l->ticks_existed;
            m.id = l->entity_id;
            m.hurt = l->hurt_time; m.death = l->death_time; m.partial_tick = pt;
            if (cl)
            {
                /* the client copy's own angles, limbs and clocks */
                /* (EntitySquid turns its own renderYawOffset from its
                 * client motion and squid clocks: the server's stands in) */
                if (mk != MOB_SQUID)
                {
                    m.body_yaw = mob_yaw(cl->prev_body, cl->body, pt);
                    m.head_yaw = mob_yaw(cl->prev_head, cl->head, pt);
                }
                m.pitch = cl->prev_pitch + (ce->pitch - cl->prev_pitch) * pt;
                m.limb = cl->limb - cl->limb_amount * (1.0f - pt);
                m.limb_amount = cl->prev_limb_amount + (cl->limb_amount - cl->prev_limb_amount) * pt;
                m.age = cl->ticks;
                m.hurt = cl->hurt;
                m.death = cl->death;
            }
            /* the copy's isBurning: its own fire (moveEntity's touch of
             * fire or lava this tick, the base tick zeroing it before) or
             * the server's flag 0 its S1C carried */
            m.burning = ce ? (ce->flags0 & 1) != 0 || cent_touches_fire(ce, e) : e->fire > 0;
            /* the kinds' watched values as the copy holds them */
            int kindw = ce ? ce->kindw : 0;
            m.child = l->kind == HK_ZOMBIE ? (ce ? (kindw & 1) : l->zombie_is_child) : l->growing_age < 0;
            /* watcher 16 as the client's copy holds it (its S1Cs and the
             * client half of a dye or a saddle), else the server's */
            int dw16 = ce ? ce->dw16 : l->data_watcher_16;
            /* EntityBlaze.isBurning is func_70845_n, its charging flag
             * (watcher 16 bit 0), not its fire: Render.doRenderShadowAndFire
             * draws the fire while it charges */
            if (mk == MOB_BLAZE) m.burning = (dw16 & 1) != 0;
            m.color = dw16 & 15;
            m.sheared = (dw16 & 16) != 0;
            m.saddle = dw16 != 0;
            m.charged = ce ? (kindw >> 8) & 1 : l->creeper_powered;
            m.carried_id = ce ? kindw & 255 : l->enderman_carried_block;
            m.carried_meta = ce ? (kindw >> 8) & 255 : l->enderman_carrying_data;
            m.screaming = ce ? (kindw >> 16) & 1 : l->enderman_screaming;
            m.slime_size = l->slime_size;
            /* the copy's own squish (its prevSquishFactor equals it), else the server's */
            m.squish = ce ? ce->squish_factor : l->prev_squish_factor + (l->squish_factor - l->prev_squish_factor) * pt;
            if (mk == MOB_CREEPER && l->creeper_fuse_time > 2)
            {
                /* the copy's own clocks (EntityCreeper.onUpdate on the
                 * client, over its watched state) */
                int last = cl ? cl->creeper_last : l->creeper_last_active_time;
                int since = cl ? cl->creeper_since : l->creeper_time_since_ignited;
                m.flash = (last + (since - last) * pt) / (float)(l->creeper_fuse_time - 2);
            }
            m.profession = l->profession;
            /* the client copy's equipment (its S04s), else the server's */
            for (int k = 0; k < 5; ++k) equip_item(cl ? &cl->equip[k] : &l->equip[k], &m.equip[k]);
            m.zombie_villager = l->kind == HK_ZOMBIE && (ce ? (kindw >> 1) & 1 : l->zombie_is_villager);
            m.converting = l->kind == HK_ZOMBIE && (ce ? (kindw >> 2) & 1 : l->zombie_is_converting);
            m.ghast_shooting = dw16 != 0;
            m.attack_progress = (l->prev_attack_counter + (l->attack_counter - l->prev_attack_counter) * pt) / 20.0f;
            m.attack_timer = l->attack_timer;
            m.rose_timer = l->hold_rose_tick;
            m.tentacle = l->last_tentacle_angle + (l->tentacle_angle - l->last_tentacle_angle) * pt;
            m.squid_pitch = l->prev_squid_pitch + (l->squid_pitch - l->prev_squid_pitch) * pt;
            m.squid_yaw = l->prev_squid_yaw + (l->squid_yaw - l->prev_squid_yaw) * pt;
            m.hanging = (l->data_watcher_16 & 1) != 0;
            /* getBrightnessForRender: the copy's posX, posY and posZ, not
             * the interpolated ones */
            double lx = ce ? ce->x : e->pos_x, ly = ce ? ce->y : e->pos_y, lz = ce ? ce->z : e->pos_z;
            int bx = (int)floor(lx), by = (int)floor(ly + e->height * 0.66),
                bz = (int)floor(lz);
            m.brightness = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
                           (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
            m.brightness_scalar = ent_brightness(m.brightness);
            mob_self_lit(&m);
            m.now = frame_now_ms;
            /* the client copy's lead (its S1Bs), else the server's */
            int lh = cl ? cl->leash_holder : l->leash_holder;
            uint64_t lk = cl ? cl->leash_knot : l->leash_knot;
            if (lh == LEASH_KNOT || lh == LEASH_PLAYER)
            {
                /* RenderLiving.func_110827_b toward the holder: a knot's
                 * copy (EntityHanging: straight down its tile's centre), or
                 * the client player at 0.7 of its eye height */
                double hx = 0, hy = 0, hz = 0;
                int have = 0;
                if (lh == LEASH_KNOT && lk)
                {
                    uint32_t ki = (uint32_t)lk;
                    const fh_ent *kn = ((lk >> 32) & 0xFFFFu) == (nw_arena()->fh_ents.gen[ki] & 0xFFFFu)
                                       ? fh_ent_at((int32_t)ki) : NULL;
                    int nobj = 0;
                    const struct pickobj *objs = kn && CP.pickobj ? pickobj_client_objs(&CP, &nobj) : NULL;
                    for (int k = 0; k < nobj && !have; ++k)
                        if (objs[k].id == kn->entity_id && !objs[k].dead)
                        {
                            hx = objs[k].e.pos_x;
                            hy = objs[k].e.pos_y + 0.5 - 0.25;
                            hz = objs[k].e.pos_z;
                            have = 1;
                        }
                }
                else if (lh == LEASH_PLAYER)
                {
                    double ry = (double)(pyaw + (yaw - pyaw) * (pt * 0.5F)) * 0.01745329238474369;
                    double rp = (double)(ppitch + (pitch - ppitch) * (pt * 0.5F)) * 0.01745329238474369;
                    double c = cos(ry), sn = sin(ry), sp = sin(rp), cp = cos(rp);
                    double eye = (double)0.12F * 0.7;
                    hx = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt - c * 0.7 - sn * 0.5 * cp;
                    hy = CP.e.prev_pos_y + eye + (CP.e.pos_y - CP.e.prev_pos_y) * pt - sp * 0.5 - 0.25;
                    hz = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt - sn * 0.7 + c * 0.5 * cp;
                    have = 1;
                }
                if (have)
                {
                    float pb = cl ? cl->prev_body : l->prev_render_yaw_offset, bb = cl ? cl->body : l->render_yaw_offset;
                    double a = (double)(pb + (bb - pb) * pt) * 0.01745329238474369 + M_PI / 2.0;
                    double ox = cos(a) * (double)e->width * 0.4, oz = sin(a) * (double)e->width * 0.4;
                    m.leash = 1;
                    m.leash_start[0] = (float)ox;
                    m.leash_start[1] = (float)(-(1.6 - (double)e->height) * 0.5);
                    m.leash_start[2] = (float)oz;
                    m.leash_run[0] = (float)(hx - (m.x + ox));
                    m.leash_run[1] = (float)(hy - m.y);
                    m.leash_run[2] = (float)(hz - (m.z + oz));
                }
            }
            if (mk == MOB_CHICKEN) {
                float flap = l->chicken_field_70888_h +
                    (l->chicken_field_70886_e - l->chicken_field_70888_h) * pt;
                float power = l->chicken_field_70884_g +
                    (l->chicken_dest_pos - l->chicken_field_70884_g) * pt;
                m.wing = (sinf(flap) + 1.0f) * power;
            }
            if (mk == MOB_SHEEP && l->sheep_timer > 0) {
                int st = l->sheep_timer;
                m.eat_head_y = st >= 4 && st <= 36 ? 1.0f :
                    st < 4 ? (st - pt) / 4.0f : -(st - 40 - pt) / 4.0f;
                m.eat_head_x = 3.1415927f / 5.0f;
            }
            if (cl)
            {
                struct cent_living *keep = cent_find(l->entity_id);
                keep->last_m = m;
                keep->has_last_m = 1;
                keep->last_fuse = l->creeper_fuse_time;
            }
            int drew = raster_mobs_draw(assets, &target, &m, 1, &RW,
                raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
            n += drew;
            /* RenderGlobal.renderEntities draws a slime in range whose box
             * is in the frustum; its pass 0 turns GL_NORMALIZE on for good */
            if (mk == MOB_SLIME && drew && !gl_normalize)
            {
                struct aabb bb = e->bounding_box;
                if (ce)
                {
                    double hw = (double)(e->width / 2.0f);
                    bb.min_x = ce->x - hw; bb.max_x = ce->x + hw;
                    bb.min_y = ce->y; bb.max_y = ce->y + e->height;
                    bb.min_z = ce->z - hw; bb.max_z = ce->z + hw;
                }
                gl_normalize = box_in_frustum(o, &bb);
            }
            continue;
        }
        draw_box(rgb, depth, o, e->bounding_box, (e->prev_pos_x - e->pos_x) * (1 - pt),
                 (e->prev_pos_y - e->pos_y) * (1 - pt), (e->prev_pos_z - e->pos_z) * (1 - pt),
                 hostile(lv_get(a->livh)->kind) ? red : white);
        ++n;
    }
    /* WorldClient's copies whose server entity left the world this row (an
     * exploded creeper, a mob removed after its death animation): the
     * client still holds and draws them until the S13 lands at its next
     * tick */
    if (SR.combat && ncents) cent_index_build();
    for (int i = 0; SR.combat && i < ncents; ++i)
    {
        const struct cent_living *c = &cents[i];
        if (!c->has_last_m || c->dead || cent_server_living(c->id)) continue;
        const struct client_entity *ce = cwidx_get(&SR.combat->client, c->id);
        if (!ce || ce->is_dead) continue;
        struct mob_render_input m = c->last_m;
        m.x = ce->prev_x + (ce->x - ce->prev_x) * pt;
        m.y = ce->prev_y + (ce->y - ce->prev_y) * pt;
        m.z = ce->prev_z + (ce->z - ce->prev_z) * pt;
        if (m.kind != MOB_SQUID)
        {
            m.body_yaw = mob_yaw(c->prev_body, c->body, pt);
            m.head_yaw = mob_yaw(c->prev_head, c->head, pt);
        }
        m.pitch = c->prev_pitch + (ce->pitch - c->prev_pitch) * pt;
        m.limb = c->limb - c->limb_amount * (1.0f - pt);
        m.limb_amount = c->prev_limb_amount + (c->limb_amount - c->prev_limb_amount) * pt;
        m.age = c->ticks;
        m.hurt = c->hurt;
        m.death = c->death;
        m.partial_tick = pt;
        m.view_yaw = pyaw + (yaw - pyaw) * pt;
        m.burning = m.kind == MOB_BLAZE ? (ce->dw16 & 1) != 0 : (ce->flags0 & 1) != 0 || cent_touches_fire(ce, NULL);
        if (m.kind == MOB_CREEPER && c->last_fuse > 2)
            m.flash = (c->creeper_last + (c->creeper_since - c->creeper_last) * pt) / (float)(c->last_fuse - 2);
        int bx = (int)floor(ce->x), by = (int)floor(ce->y + ce->height * 0.66), bz = (int)floor(ce->z);
        m.brightness = (world_get_light(view_world(), LIGHT_SKY, bx, by, bz) << 20) |
                       (world_get_light(view_world(), LIGHT_BLOCK, bx, by, bz) << 4);
        m.brightness_scalar = ent_brightness(m.brightness);
        mob_self_lit(&m);
        m.now = frame_now_ms;
        n += raster_mobs_draw(assets, &target, &m, 1, &RW,
            raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
    }
    cent_index_drop();
    draw_things(&target, pt, assets);
    /* The End's boss: the dragon runs in dragon.c (not an anw living) and the
     * crystals are not in the item lists, so neither reaches the loops above.
     * RenderManager.renderEntityWithPosYaw over WorldClient's loadedEntityList
     * draws them: the dragon via RenderDragon (ModelDragon's 24 interpolated
     * movement offsets, RenderDragon.doRender's healing beam), the crystals
     * via RenderEnderCrystal (innerRotation + pt). The dragon's prevPos
     * smoothing is on the dragon side only (a crystal has no prevPos). */
    struct world *vw = view_world();
    const struct dragon_state *d = SR.end != NULL && SR.end->dragon_listed &&
                             !SR.end->d.dead ? &SR.end->d : NULL;
    /* WorldClient draws its own EntityDragon (pickobj.c's copy): none while
     * the tracker has not sent it */
    const struct dragon_interp *cip = NULL;
    const struct pickobj *cdo = cent_dragon_copy();
    const struct dragon_state *cd = cdo && cdragon.used && cdragon.id == cdo->id ? pickobj_client_dragon(cdo, &cip) : NULL;
    if (CP.pickobj && !cd) d = NULL;
    if (d != NULL)
    {
        int heal = d->healing_crystal_index;
        const struct dragon_crystal_state *healing =
            heal >= 0 && heal < d->n_crystals && d->crystals[heal].in_world &&
            !d->crystals[heal].dead ? &d->crystals[heal] : NULL;
        struct mob_render_input m = {0};
        m.kind = MOB_DRAGON;
        m.x = d->prev_x + (d->x - d->prev_x) * pt;
        m.y = d->prev_y + (d->y - d->prev_y) * pt;
        m.z = d->prev_z + (d->z - d->prev_z) * pt;
        m.height = 8.0f;
        m.width = 16.0f;
        m.age = d->ticks;
        m.anim = d->prev_anim + (d->anim - d->prev_anim) * pt;
        m.death_ticks = d->death_ticks;
        m.hurt = d->hurt_time;
        m.partial_tick = pt;
        /* the boss bar's health: IBossDisplayData over the 200 max health */
        m.health = d->health;
        m.max_health = 200.0f;
        if (cd)
        {
            /* the client's EntityDragon: its lastTickPos, animTime, hurt
             * clock and watched health */
            m.x = cdragon.prev_x + (cdragon.x - cdragon.prev_x) * pt;
            m.y = cdragon.prev_y + (cdragon.y - cdragon.prev_y) * pt;
            m.z = cdragon.prev_z + (cdragon.z - cdragon.prev_z) * pt;
            m.age = cdragon.ticks;
            m.anim = cdragon.prev_anim + (cdragon.anim - cdragon.prev_anim) * pt;
            m.death_ticks = cd->death_ticks;
            m.hurt = cdragon.hurt;
            m.health = cip->health;
            d = cd;
        }
        /* EntityDragon.getMovementOffsets over the client ring: each entry
         * interpolates toward the one behind it (pt 1 is this tick's value,
         * read from index ring_index - n; dead dragons read pt 0). */
        double fac = m.health <= 0.0f ? 0.0 : 1.0 - (double)pt;
        for (int i = 0; i < 24; ++i)
        {
            int a = (d->ring_index - i) & 63, b = (d->ring_index - i - 1) & 63;
            double dyaw = d->ring[b][0] - d->ring[a][0];
            dyaw = fmod(dyaw, 360.0);
            if (dyaw >= 180.0) dyaw -= 360.0;
            if (dyaw < -180.0) dyaw += 360.0;
            m.off[i][0] = d->ring[a][0] + dyaw * fac;
            m.off[i][1] = d->ring[a][1] + (d->ring[b][1] - d->ring[a][1]) * fac;
            m.off[i][2] = d->ring[a][2] + (d->ring[b][2] - d->ring[a][2]) * fac;
        }
        int bx = (int)floor(m.x), by = (int)floor(m.y + 8.0 * 0.66), bz = (int)floor(m.z);
        m.brightness = (world_get_light(vw, LIGHT_SKY, bx, by, bz) << 20) |
                       (world_get_light(vw, LIGHT_BLOCK, bx, by, bz) << 4);
        m.brightness_scalar = ent_brightness(m.brightness);
        m.now = frame_now_ms;
        if (healing != NULL)
        {
            /* RenderDragon.doRender's beam inputs: the crystal offset, its
             * innerRotation + pt, and the dragon's own prevPos smoothing. */
            double sm = 1.0 - (double)pt;
            double bx = cd ? cdragon.x : d->x, by = cd ? cdragon.y : d->y, bz = cd ? cdragon.z : d->z;
            double bpx = cd ? cdragon.prev_x : d->prev_x, bpy = cd ? cdragon.prev_y : d->prev_y,
                   bpz = cd ? cdragon.prev_z : d->prev_z;
            m.has_beam = 1;
            m.beam_rot = (float)((double)healing->inner_rotation + pt);
            m.beam_dx = (float)(healing->x - bx - (bpx - bx) * sm);
            m.beam_dy = (float)((double)(mh_sin(m.beam_rot * 0.2f) / 2.0f + 0.5f) +
                                healing->y - 1.0 - by - (bpy - by) * sm);
            m.beam_dz = (float)(healing->z - bz - (bpz - bz) * sm);
        m.beam_len = (float)sqrt((double)m.beam_dx * m.beam_dx +
                                 (double)m.beam_dy * m.beam_dy +
                                 (double)m.beam_dz * m.beam_dz);
            m.beam_ticks = m.age;
        }
        n += raster_mobs_draw(assets, &target, &m, 1, &RW,
            raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
    }
    if (SR.end != NULL)
    {
        for (int i = 0; i < SR.end->d.n_crystals; ++i)
        {
            const struct dragon_crystal_state *c = &SR.end->d.crystals[i];
            if (!c->in_world || c->dead) continue;
            struct mob_render_input m = {0};
            m.kind = MOB_ENDER_CRYSTAL;
            m.x = c->x;
            m.y = c->y;
            m.z = c->z;
            m.height = 2.0f;
            m.width = 2.0f;
            m.id = c->entity_id;
            m.age = c->inner_rotation;
            m.crystal_rot = (float)((double)c->inner_rotation + pt);
            m.partial_tick = pt;
            int bx = (int)floor(m.x), by = (int)floor(m.y + 2.0 * 0.66), bz = (int)floor(m.z);
            m.brightness = (world_get_light(vw, LIGHT_SKY, bx, by, bz) << 20) |
                           (world_get_light(vw, LIGHT_BLOCK, bx, by, bz) << 4);
            m.brightness_scalar = ent_brightness(m.brightness);
            m.now = frame_now_ms;
            n += raster_mobs_draw(assets, &target, &m, 1, &RW,
                raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
        }
    }
    /* WorldClient's hanging entities (pickobj.c's copies): RenderPainting,
     * RenderItemFrame (its item and rotation the server frame's watcher) and
     * RenderLeashKnot, each where its tile puts it */
    {
        int nobj = 0;
        const struct pickobj *objs = CP.pickobj ? pickobj_client_objs(&CP, &nobj) : NULL;
        int aw = 0, ah = 0;
        const unsigned char *atlas = raster_live_block_atlas(RL, &aw, &ah);
        for (int i = 0; i < nobj; ++i)
        {
            const struct pickobj *ob = &objs[i];
            if (ob->dead || (ob->kind != PK_PAINTING && ob->kind != PK_FRAME && ob->kind != PK_KNOT)) continue;
            /* EntityHanging.setDirection's yaw: north and south turn opposite */
            float hyaw = (float)((ob->dir == 0 || ob->dir == 2 ? (ob->dir + 2) % 4 : ob->dir) * 90);
            if (ob->kind == PK_PAINTING)
            {
                raster_painting_live("out/assets/misc", &target, ob->art, ob->dir, hyaw, ob->e.pos_x, ob->e.pos_y,
                                     ob->e.pos_z, &RW);
                ++n;
            }
            else if (ob->kind == PK_FRAME)
            {
                struct raster_itemframe f;
                memset(&f, 0, sizeof f);
                f.tile[0] = ob->tile_x; f.tile[1] = ob->tile_y; f.tile[2] = ob->tile_z;
                f.dir = ob->dir;
                f.pos[0] = ob->e.pos_x; f.pos[1] = ob->e.pos_y; f.pos[2] = ob->e.pos_z;
                f.yaw = hyaw;
                const struct aabb *b = &ob->e.bounding_box;
                f.box[0] = b->min_x; f.box[1] = b->min_y; f.box[2] = b->min_z;
                f.box[3] = b->max_x; f.box[4] = b->max_y; f.box[5] = b->max_z;
                const fh_world *fw = &SR.d->fhw;
                for (int k = 0; k < fw->n; ++k)
                {
                    const fh_ent *h = fh_ent_at(fw->slot[k]);
                    if (!h || h->entity_id != ob->id) continue;
                    f.item_id = h->item;
                    f.item_damage = h->item_damage;
                    f.item_rot = h->rot & 3;
                }
                raster_itemframe_draw_live(assets, hud_assets, &target, &f, &RW, atlas, aw, ah);
                ++n;
            }
            else
            {
                struct mob_render_input m = {0};
                m.kind = MOB_LEASH_KNOT;
                m.x = ob->e.pos_x;
                m.y = ob->e.pos_y;
                m.z = ob->e.pos_z;
                m.width = m.height = 0.5f;
                m.partial_tick = pt;
                int bx = (int)floor(m.x), by = (int)floor(ob->e.bounding_box.min_y + 0.5 * 0.66), bz = (int)floor(m.z);
                m.brightness = (world_get_light(vw, LIGHT_SKY, bx, by, bz) << 20) |
                               (world_get_light(vw, LIGHT_BLOCK, bx, by, bz) << 4);
                m.brightness_scalar = ent_brightness(m.brightness);
                m.now = frame_now_ms;
                n += raster_mobs_draw(assets, &target, &m, 1, &RW,
                    raster_live_mesher(RL)->tab, play_fancy >= 0 ? play_fancy : fancy_assets, lbt);
            }
        }
    }
    /* The dropped items and XP orbs draw their own models: RenderItem's flat
     * path, its 3D block path (renderBlockAsItem) and RenderXPOrb. An item
     * the icon table does not know keeps its hitbox. */
    /* WorldClient's own EntityItem and EntityXPOrb copies (clientents.c) */
    static struct worldfx_entity ents[WFX_MAX];
    int nents_wfx = 0;
    for (int i = 0; i < ncitems && nents_wfx < WFX_MAX && !shot_skip_drops; ++i)
    {
        const struct citem *c = &citems[i];
        /* RenderGlobal.renderEntities' isInRangeToRender3d: an item's 0.25
         * box draws within 16 blocks of the camera, an orb's 0.5 within 32 */
        if (!c->has_copy || !thing_in_range(&c->e, 1.0, cam)) continue;
        struct worldfx_entity *w = &ents[nents_wfx];
        citem_fill(w, c, pt);
        if (!w->orb && !raster_hud_item_entry(hud_assets, c->item, c->damage))
        {
            draw_box(rgb, depth, o, c->e.bounding_box, (c->prev_x - c->e.pos_x) * (1 - pt),
                     (c->prev_y - c->e.pos_y) * (1 - pt), (c->prev_z - c->e.pos_z) * (1 - pt), yellow);
            ++n;
            continue;
        }
        ++nents_wfx;
    }
    /* the pickups: EntityPickupFX.renderParticle's path toward the player */
    for (int i = 0; i < npickups && nents_wfx < WFX_MAX && !shot_skip_drops; ++i)
    {
        const struct pickup_fx *f = &pickups[i];
        if (f->resting)
        {
            if (!f->copy) ents[nents_wfx++] = f->e;
            continue;
        }
        float v8 = ((float)f->age + pt) / 3.0F;
        v8 *= v8;
        double bx = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * (double)pt;
        double by = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * (double)pt + (double)-0.5F;
        double bz = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * (double)pt;
        struct worldfx_entity *w = &ents[nents_wfx++];
        *w = f->e;
        double ip[3] = {CP.e.prev_pos_x, CP.e.prev_pos_y, CP.e.prev_pos_z};
        if (fx_interp_set) memcpy(ip, fx_interp, sizeof ip);
        double cx = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * (double)pt;
        double cy = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * (double)pt;
        double cz = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * (double)pt;
        w->x = cx + (double)(float)(f->x + (bx - f->x) * (double)v8 - ip[0]);
        w->y = cy + (double)(float)(f->y + (by - f->y) * (double)v8 - ip[1]);
        w->z = cz + (double)(float)(f->z + (bz - f->z) * (double)v8 - ip[2]);
    }
    if (nents_wfx) {
        struct entity_raster_target wt = {.w = W, .h = H, .proj = o->proj, .mv = o->mv,
            .cam = cam, .fog = o->gfogc, .fogs = o->gfogs, .foge = o->gfoge,
            .fogd = o->gfogd, .fogm = o->gfogm, .lm = (const uint32_t *)o->lm,
            .depth = raster_live_depth_mut(RL), .rgb = rgb, .material = {1, 1, 1}};
        /* RenderManager's playerViewY/playerViewX: the render view entity's
         * interpolated yaw and pitch, which the billboards rotate by. */
        int aw = 0, ah = 0;
        const unsigned char *atlas = raster_live_block_atlas(RL, &aw, &ah);
        n += raster_entities_live(assets, hud_assets, &wt, ents, nents_wfx, pt,
                                  pyaw + (yaw - pyaw) * pt, ppitch + (pitch - ppitch) * pt,
                                  atlas, aw, ah);
    }
    return n;
}

/* ------------------------------------------------------------- tile entities */

/* The client's spawner (MobSpawnerBaseLogic's client half): the live
 * client shares the server's world, whose tile entities carry no rotation,
 * so the client's own lives here per spawner. The client's tile entity is
 * made by the S35 description packet that follows the spawner's block into
 * the client world (the chunk or the block change), in the same pump, and
 * its spawnDelay starts as that packet's Delay: the server's desc_delay of
 * the tick that built it, one tick before (the flush and the chunk sends run
 * before the server's tile entities; a spawner the session starts with takes
 * the server's delay). Each client tick while the player is within the
 * required range it counts down to 0 and the spin turns by
 * 1000 / (delay + 200). A server resetTimer's block event 1 never reaches the
 * client (the server's own receiveClientEvent answers false, so no S24 goes
 * out), so the client's delay stays at 0 once there. */
static struct spin { int x, y, z, delay, live, made, desc; double rot, prev; } spins[256];
static int nspins, spins_seen;

static struct spin *spin_of(int x, int y, int z, const struct te_spawner *sp)
{
    for (int i = 0; i < nspins; ++i)
        if (spins[i].x == x && spins[i].y == y && spins[i].z == z) return &spins[i];
    if (nspins >= 256) return NULL;
    spins[nspins] = (struct spin){x, y, z, sp->spawn_delay, 0, 0, sp->desc_delay, 0.0, 0.0};
    return &spins[nspins++];
}

/* One client tick of every spawner, after the row (not while paused). */
/* The client's own TileEntityEnchantmentTable per table: updateEntity runs on
 * the client world every client tick, the reader its closest player within
 * 3 (the client player), the page picks drawn from the class's static Random
 * on the client's stream (Det.splitRandom's CLIENT role). A new table's
 * client entity starts at the constructor's zeros and, made by a packet
 * after the row's updateEntities, first updates on the next row; one the
 * session starts with updates at once. */
static struct cbook
{
    int x, y, z, live;
    int wait;                               /* rows before its first update */
    int ticks;                              /* field_145926_a */
    float flip, prev_flip, flip_t, flip_a;  /* field_145933_i, _145931_j, _145932_k, _145929_l */
    float spread, prev_spread;              /* field_145930_m, field_145927_n */
    float rot, prev_rot, t_rot;             /* field_145928_o, field_145925_p, field_145924_q */
} cbooks[64];
static int ncbooks, cbooks_seen;

static int cbook_rand(int n)
{
    static const char *const name = "./net/minecraft/tileentity/TileEntityEnchantmentTable.java:field_145923_r";
    det_state *det = &SR_DET(&SR);
    det_split *sp = det_split_find(det, name);
    if (sp == NULL) sp = det_split_random(det, name);
    return det_split_int_n_role(det, sp, DET_CLIENT, n);
}

static void cbook_update(struct cbook *b)
{
    const float pi = 3.1415927F;
    b->prev_spread = b->spread;
    b->prev_rot = b->rot;
    double cx = (double)((float)b->x + 0.5F), cy = (double)((float)b->y + 0.5F), cz = (double)((float)b->z + 0.5F);
    double dx = CP.e.pos_x - cx, dy = CP.e.pos_y - cy, dz = CP.e.pos_z - cz;
    if (dx * dx + dy * dy + dz * dz < 9.0)
    {
        b->t_rot = (float)fd_atan2(dz, dx);
        b->spread += 0.1F;
        if (b->spread < 0.5F || cbook_rand(40) == 0)
        {
            float was = b->flip_t;
            do
            {
                int p = cbook_rand(4);
                int q = cbook_rand(4);
                b->flip_t += (float)(p - q);
            }
            while (was == b->flip_t);
        }
    }
    else
    {
        b->t_rot += 0.02F;
        b->spread -= 0.1F;
    }
    while (b->rot >= pi) b->rot -= pi * 2.0F;
    while (b->rot < -pi) b->rot += pi * 2.0F;
    while (b->t_rot >= pi) b->t_rot -= pi * 2.0F;
    while (b->t_rot < -pi) b->t_rot += pi * 2.0F;
    float turn;
    for (turn = b->t_rot - b->rot; turn >= pi; turn -= pi * 2.0F) {}
    while (turn < -pi) turn += pi * 2.0F;
    b->rot += turn * 0.4F;
    if (b->spread < 0.0F) b->spread = 0.0F;
    if (b->spread > 1.0F) b->spread = 1.0F;
    ++b->ticks;
    b->prev_flip = b->flip;
    float step = (b->flip_t - b->flip) * 0.4F;
    if (step < -0.2F) step = -0.2F;
    if (step > 0.2F) step = 0.2F;
    b->flip_a += (step - b->flip_a) * 0.9F;
    b->flip += b->flip_a;
}

static const struct cbook *cbook_of(int x, int y, int z)
{
    for (int i = 0; i < ncbooks; ++i)
        if (cbooks[i].x == x && cbooks[i].y == y && cbooks[i].z == z) return &cbooks[i];
    return NULL;
}

static void tileents_tick(void)
{
    struct world *w = view_world();
    for (int i = 0; i < ncbooks; ++i) cbooks[i].live = 0;
    for (int i = 0; w && i < w->te_n; ++i)
    {
        struct tile_entity *te = w->te_list[i];
        if (!te || te->invalid || te->kind != TE_ENCHANT_TABLE) continue;
        /* the client's entity comes with the table's block in the client
         * world (the S23 or chunk that carries it) */
        if (CW && (world_get_block(CW, te->x, te->y, te->z) & 4095) != 116) continue;
        struct cbook *b = (struct cbook *)cbook_of(te->x, te->y, te->z);
        if (!b)
        {
            if (ncbooks == 64) continue;
            b = &cbooks[ncbooks++];
            memset(b, 0, sizeof *b);
            b->x = te->x; b->y = te->y; b->z = te->z;
            b->wait = cbooks_seen ? 1 : 0;
        }
        b->live = 1;
        if (b->wait > 0) { --b->wait; continue; }
        cbook_update(b);
    }
    cbooks_seen = 1;
    {
        int k = 0;
        for (int i = 0; i < ncbooks; ++i) if (cbooks[i].live) cbooks[k++] = cbooks[i];
        ncbooks = k;
    }
    for (int i = 0; i < nspins; ++i) spins[i].live = 0;
    for (int i = 0; w && i < w->te_n; ++i)
    {
        struct tile_entity *te = w->te_list[i];
        if (!te || te->invalid || te->kind != TE_MOB_SPAWNER) continue;
        const struct te_spawner *sp = &te->u.spawner;
        struct spin *s = spin_of(te->x, te->y, te->z, sp);
        if (!s) continue;
        s->live = 1;
        if (!s->made)
        {
            /* not in the client world yet: the Delay an S35 built this
             * tick would carry */
            if (CW && (world_get_block(CW, te->x, te->y, te->z) & 4095) != 52)
            {
                s->desc = sp->desc_delay;
                continue;
            }
            s->made = 1;
            s->delay = spins_seen ? s->desc : sp->spawn_delay;
        }
        double dx = CP.e.pos_x - (te->x + 0.5), dy = CP.e.pos_y - (te->y + 0.5), dz = CP.e.pos_z - (te->z + 0.5);
        double range = (double)sp->required_player_range;
        if (!(dx * dx + dy * dy + dz * dz < range * range)) continue;
        raster_tileents_spawner_tick(&s->rot, &s->prev, &s->delay);
    }
    spins_seen = 1;
    int k = 0;
    for (int i = 0; i < nspins; ++i) if (spins[i].live) spins[k++] = spins[i];
    nspins = k;
}

/* The client's chest and ender chest lids: the client tile entity's
 * updateEntity steps them on the players-using count the S24 block event
 * carries, which the server sends in its next world tick and the client
 * reads at the tick after that, so the client's lid (and previous lid) is
 * the server's of one row before. A new chest's client entity starts shut. */
static struct clid { int x, y, z, live; float lid, prev, next_lid, next_prev; } clids[512];
static int nclids;

static void chest_lids_after_row(void)
{
    struct world *w = view_world();
    for (int i = 0; i < nclids; ++i) clids[i].live = 0;
    for (int i = 0; w && i < w->te_n; ++i)
    {
        const struct tile_entity *te = w->te_list[i];
        if (!te || te->invalid || (te->kind != TE_CHEST && te->kind != TE_ENDER_CHEST)) continue;
        struct clid *c = NULL;
        for (int k = 0; k < nclids && !c; ++k)
            if (clids[k].x == te->x && clids[k].y == te->y && clids[k].z == te->z) c = &clids[k];
        if (!c)
        {
            if (nclids == 512) continue;
            c = &clids[nclids++];
            *c = (struct clid){te->x, te->y, te->z, 0, 0.0F, 0.0F, 0.0F, 0.0F};
        }
        c->live = 1;
        c->lid = c->next_lid;
        c->prev = c->next_prev;
        c->next_lid = te->u.chest.lid;
        c->next_prev = te->u.chest.prev_lid;
    }
    int k = 0;
    for (int i = 0; i < nclids; ++i) if (clids[i].live) clids[k++] = clids[i];
    nclids = k;
}

static void chest_lids_reset(void)
{
    /* a new WorldClient's tile entities: the lids, the spawners, the books */
    nclids = 0;
    nspins = 0;
    spins_seen = 0;
    ncbooks = 0;
    cbooks_seen = 0;
}

static const struct clid *chest_lid_of(int x, int y, int z)
{
    for (int i = 0; i < nclids; ++i)
        if (clids[i].x == x && clids[i].y == y && clids[i].z == z) return &clids[i];
    return NULL;
}

/* RenderGlobal.renderEntities' block-entity loop over the live world: the
 * world's tile entities (creation order: vanilla's own list order is the
 * chunk rebuilds' HashSet order, which no two runs share), each renderer's
 * state from the server's tile entity (the chest lids from the client's own,
 * chest_lids_after_row), the lightmap at the tile's own light,
 * ActiveRenderInfo's object-space camera from the frame's matrices. */
static int draw_tileents(unsigned char *rgb, const struct rs_out *o, float pt, int64_t now_tick,
                         const char *assets)
{
    struct world *w = view_world();
    if (!w || !w->te_n) return 0;
    static struct te_render_item items[1024];
    int n = 0;
    for (int i = 0; i < w->te_n && n < 1024; ++i)
    {
        const struct tile_entity *te = w->te_list[i];
        if (!te || te->invalid) continue;
        struct te_render_item *e = &items[n];
        memset(e, 0, sizeof *e);
        e->kind = te->kind == TE_CHEST ? TE_RENDER_CHEST : te->kind == TE_ENDER_CHEST ? TE_RENDER_ENDER_CHEST :
                  te->kind == TE_SIGN ? TE_RENDER_SIGN : te->kind == TE_SKULL ? TE_RENDER_SKULL :
                  te->kind == TE_MOB_SPAWNER ? TE_RENDER_SPAWNER : te->kind == TE_END_PORTAL ? TE_RENDER_END_PORTAL :
                  te->kind == TE_ENCHANT_TABLE ? TE_RENDER_ENCHANT_TABLE : TE_RENDER_NONE;
        if (e->kind == TE_RENDER_NONE) continue;
        e->x = te->x; e->y = te->y; e->z = te->z;
        e->block = world_get_block(w, te->x, te->y, te->z) & 4095;
        e->meta = world_get_meta(w, te->x, te->y, te->z);
        e->brf = world_get_light(w, LIGHT_SKY, te->x, te->y, te->z) << 20 |
                 world_get_light(w, LIGHT_BLOCK, te->x, te->y, te->z) << 4;
        e->edit = -1;
        e->mob = -1;
        if (te->kind == TE_CHEST || te->kind == TE_ENDER_CHEST)
        {
            const struct clid *cl = chest_lid_of(te->x, te->y, te->z);
            e->lid = cl ? cl->lid : 0.0F;
            e->prev_lid = cl ? cl->prev : 0.0F;
            if (te->kind == TE_CHEST)
            {
                /* chest_scan's order is z-1, z+1, x+1, x-1 */
                e->adj[0] = te->u.chest.adj[0] != 0;
                e->adj[1] = te->u.chest.adj[2] != 0;
                e->adj[2] = te->u.chest.adj[3] != 0;
                e->adj[3] = te->u.chest.adj[1] != 0;
            }
        }
        else if (te->kind == TE_SIGN)
            for (int l = 0; l < 4; ++l) snprintf(e->text[l], sizeof e->text[l], "%s", te->sign_text[l]);
        else if (te->kind == TE_SKULL)
        {
            e->skull_type = te->skull_type;
            e->skull_rot = te->skull_rot;
        }
        else if (te->kind == TE_ENCHANT_TABLE)
        {
            const struct cbook *b = cbook_of(te->x, te->y, te->z);
            if (b)
            {
                e->book_ticks = b->ticks;
                e->book_flip = b->flip; e->book_prev_flip = b->prev_flip;
                e->book_spread = b->spread; e->book_prev_spread = b->prev_spread;
                e->book_rot = b->rot; e->book_prev_rot = b->prev_rot;
            }
        }
        else if (te->kind == TE_MOB_SPAWNER)
        {
            e->mob = raster_tileents_mob_kind(te->u.spawner.mob_id);
            struct spin *s = spin_of(te->x, te->y, te->z, &te->u.spawner);
            if (s) { e->rot = s->rot; e->prev_rot = s->prev; }
            /* no client tile entity yet (its S35 has not arrived): nothing
             * to draw the mob from */
            if (s && !s->made && spins_seen) e->mob = -1;
            /* the display mob never ticks and setLocationAndAngles zeroes its
             * yaw and pitch each frame: RendererLivingEntity turns the body 0
             * and the head from prevRotationYawHead 0 to the constructor's
             * rotationYawHead, the Det.PIN_SPAWNER draw (a tape made
             * before the pin drew it from the shared stream: 0 here) */
            float hd = SS.spawner_pin ? det_spawner_display_yaw(S.seed) : 0.0f;
            while (hd < -180.0f) hd += 360.0f;
            while (hd >= 180.0f) hd -= 360.0f;
            e->mob_head_yaw = 0.0f + pt * hd;
        }
        ++n;
    }
    if (!n) return 0;
    struct te_render_frame f;
    memset(&f, 0, sizeof f);
    f.view[0] = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * (double)pt;
    f.view[1] = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * (double)pt;
    f.view[2] = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * (double)pt;
    f.pt = pt;
    f.normalize = gl_normalize;
    f.ms = (long long)now_tick * 50LL;
    {
        /* gluUnProject of the screen centre at window z 0 */
        double m[16], inv[16];
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                double s = 0.0;
                for (int k = 0; k < 4; ++k) s += (double)o->proj[k * 4 + r] * (double)o->mv[c * 4 + k];
                m[c * 4 + r] = s;
            }
        /* Gauss-Jordan on the 4x4 (column-major kept: invert the transpose's transpose) */
        double a[4][8];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) { a[r][c] = m[c * 4 + r]; a[r][c + 4] = r == c; }
        int ok = 1;
        for (int c = 0; c < 4 && ok; ++c)
        {
            int p = c;
            for (int r = c + 1; r < 4; ++r) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
            if (fabs(a[p][c]) < 1e-30) { ok = 0; break; }
            for (int k = 0; k < 8; ++k) { double t = a[c][k]; a[c][k] = a[p][k]; a[p][k] = t; }
            double d = a[c][c];
            for (int k = 0; k < 8; ++k) a[c][k] /= d;
            for (int r = 0; r < 4; ++r)
                if (r != c) { double q = a[r][c]; for (int k = 0; k < 8; ++k) a[r][k] -= q * a[c][k]; }
        }
        if (ok)
        {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) inv[c * 4 + r] = a[r][c + 4];
            double v[4];
            for (int r = 0; r < 4; ++r) v[r] = -inv[8 + r] + inv[12 + r];
            if (v[3] != 0.0)
                for (int k = 0; k < 3; ++k) f.object[k] = (float)(v[k] / v[3]);
        }
    }
    double cam[3] = {o->camx, o->camy, o->camz};
    struct entity_raster_target target = {.w = W, .h = H, .proj = o->proj, .mv = o->mv,
        .cam = cam, .fog = o->gfogc, .fogs = o->gfogs, .foge = o->gfoge, .fogd = o->gfogd,
        .fogm = o->gfogm, .lm = (const uint32_t *)o->lm,
        .depth = raster_live_depth_mut(RL), .rgb = rgb, .material = {1, 1, 1}};
    return raster_tileents_draw(assets, &target, &f, items, n);
}

/* ------------------------------------------------------------------- input */

/* The key each binding is on: vanilla 1.7.10's defaults (the player list
 * and pick block have none here: this client has no player list, and pick
 * block is the middle button), or the player's own from --options. */
static SDL_Keycode binds[K_N] = {
    [K_FORWARD] = SDLK_W, [K_LEFT] = SDLK_A, [K_BACK] = SDLK_S, [K_RIGHT] = SDLK_D,
    [K_JUMP] = SDLK_SPACE, [K_SNEAK] = SDLK_LSHIFT, [K_SPRINT] = SDLK_LCTRL,
    [K_DROP] = SDLK_Q, [K_INVENTORY] = SDLK_E, [K_CHAT] = SDLK_T, [K_COMMAND] = SDLK_SLASH,
    [K_PERSPECTIVE] = SDLK_F5,
    [K_HOTBAR] = SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
};

/* --options FILE: the player's own settings from a Minecraft options.txt
 * (1.7.10's, with LWJGL key codes, or a later version's with key names, as
 * Prism or the launcher writes it), only what changes no game state: the key
 * bindings (the tape records the actions they make, never the keys: the
 * oracle replays the actions), fov, gamma, mouseSensitivity and
 * invertYMouse. The render distance, bobbing, clouds and graphics stay the
 * tape header's, which the oracle recorded. Mouse-button bindings are not
 * read: attack, use and pick block stay on the left, right and middle buttons. */
/* a later version's key.keyboard.NAME */
static SDL_Keycode option_key_name(const char *n)
{
    static const struct { const char *name; SDL_Keycode k; } t[] = {
        {"left.control", SDLK_LCTRL}, {"right.control", SDLK_RCTRL}, {"left.shift", SDLK_LSHIFT},
        {"right.shift", SDLK_RSHIFT}, {"left.alt", SDLK_LALT}, {"right.alt", SDLK_RALT},
        {"left.win", SDLK_LGUI}, {"right.win", SDLK_RGUI}, {"space", SDLK_SPACE}, {"tab", SDLK_TAB},
        {"grave.accent", SDLK_GRAVE}, {"caps.lock", SDLK_CAPSLOCK}, {"enter", SDLK_RETURN},
        {"backspace", SDLK_BACKSPACE}, {"escape", SDLK_ESCAPE}, {"slash", SDLK_SLASH},
        {"backslash", SDLK_BACKSLASH}, {"minus", SDLK_MINUS}, {"equal", SDLK_EQUALS},
        {"left.bracket", SDLK_LEFTBRACKET}, {"right.bracket", SDLK_RIGHTBRACKET},
        {"semicolon", SDLK_SEMICOLON}, {"apostrophe", SDLK_APOSTROPHE}, {"comma", SDLK_COMMA},
        {"period", SDLK_PERIOD}, {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT},
        {"right", SDLK_RIGHT}, {"unknown", SDLK_UNKNOWN},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; ++i)
        if (!strcmp(n, t[i].name)) return t[i].k;
    if (n[0] && !n[1] && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) return (SDL_Keycode)n[0];
    if (n[0] == 'f' && n[1] >= '1' && n[1] <= '9')
    {
        int f = atoi(n + 1);
        if (f >= 1 && f <= 12) return SDLK_F1 + (SDL_Keycode)(f - 1);
    }
    return (SDL_Keycode)-1;
}

/* 1.7.10's LWJGL key code */
static SDL_Keycode option_key_code(int c)
{
    static const char row[] = "qwertyuiop\0\0\0\0asdfghjkl";
    static const char low[] = "zxcvbnm";
    if (c >= 2 && c <= 10) return SDLK_1 + (SDL_Keycode)(c - 2);
    if (c == 11) return SDLK_0;
    if (c >= 16 && c <= 38 && row[c - 16]) return (SDL_Keycode)row[c - 16];
    if (c >= 44 && c <= 50) return (SDL_Keycode)low[c - 44];
    if (c >= 59 && c <= 68) return SDLK_F1 + (SDL_Keycode)(c - 59);
    switch (c)
    {
    case 0: return SDLK_UNKNOWN;
    case 1: return SDLK_ESCAPE;
    case 15: return SDLK_TAB;
    case 28: return SDLK_RETURN;
    case 29: return SDLK_LCTRL;
    case 41: return SDLK_GRAVE;
    case 42: return SDLK_LSHIFT;
    case 53: return SDLK_SLASH;
    case 54: return SDLK_RSHIFT;
    case 56: return SDLK_LALT;
    case 57: return SDLK_SPACE;
    case 58: return SDLK_CAPSLOCK;
    case 87: return SDLK_F11;
    case 88: return SDLK_F12;
    case 157: return SDLK_RCTRL;
    case 184: return SDLK_RALT;
    default: return (SDL_Keycode)-1;
    }
}

static int load_options(const char *path)
{
    static const struct { const char *name; int b; } names[] = {
        {"forward", K_FORWARD}, {"left", K_LEFT}, {"back", K_BACK}, {"right", K_RIGHT},
        {"jump", K_JUMP}, {"sneak", K_SNEAK}, {"sprint", K_SPRINT}, {"drop", K_DROP},
        {"inventory", K_INVENTORY}, {"chat", K_CHAT}, {"command", K_COMMAND},
        {"togglePerspective", K_PERSPECTIVE},
    };
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "play: cannot read --options %s\n", path); return 0; }
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, ':');
        if (!v) continue;
        *v++ = 0;
        if (*v == '"') { ++v; v[strcspn(v, "\"")] = 0; }
        if (!strcmp(line, "fov")) opt_fov = (float)atof(v);
        else if (!strcmp(line, "gamma")) opt_gamma = (float)atof(v);
        else if (!strcmp(line, "mouseSensitivity")) opt_sens = (float)atof(v);
        else if (!strcmp(line, "invertYMouse")) opt_invert = !strcmp(v, "true");
        else if (!strncmp(line, "key_key.", 8))
        {
            const char *n = line + 8;
            int b = -1;
            if (!strncmp(n, "hotbar.", 7) && n[7] >= '1' && n[7] <= '9' && !n[8]) b = K_HOTBAR + (n[7] - '1');
            for (size_t i = 0; b < 0 && i < sizeof names / sizeof names[0]; ++i)
                if (!strcmp(n, names[i].name)) b = names[i].b;
            if (b < 0) continue;
            SDL_Keycode k = (SDL_Keycode)-1;
            if (!strncmp(v, "key.keyboard.", 13)) k = option_key_name(v + 13);
            else if ((v[0] >= '0' && v[0] <= '9') || v[0] == '-')
                k = atoi(v) < 0 ? (SDL_Keycode)-1 : option_key_code(atoi(v));
            if (k != (SDL_Keycode)-1) binds[b] = k;
        }
    }
    fclose(f);
    if (opt_fov < 0.0F) opt_fov = 0.0F;
    if (opt_fov > 1.0F) opt_fov = 1.0F;
    fprintf(stderr, "play: options %s: fov %.0f, gamma %.2f, sensitivity %.2f%s; keys:", path,
            (double)(opt_fov * 40.0F + 70.0F), (double)opt_gamma, (double)opt_sens, opt_invert ? ", inverted" : "");
    for (int b = K_FORWARD; b < K_HOTBAR; ++b)
        if (binds[b] != SDLK_UNKNOWN) fprintf(stderr, " %s=%s", key_desc(b), SDL_GetKeyName(binds[b]));
    fprintf(stderr, "\n");
    return 1;
}

static int key_for(SDL_Keycode k)
{
    if (k == SDLK_UNKNOWN) return -1;
    for (int b = 0; b < K_N; ++b)
        if (binds[b] == k) return b;
    return -1;
}

/* ------------------------------------------------------ the screens' input */

static SDL_Keycode script_key(const char *s);

/* LWJGL's code for the keys a container screen or its modifiers read, 0 for
 * the rest (keyTyped does nothing with them) */
static int lwjgl_key(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_ESCAPE: return GK_ESCAPE;
    case SDLK_LSHIFT: return GK_LSHIFT;
    case SDLK_RSHIFT: return GK_RSHIFT;
    case SDLK_LCTRL: return GK_LCONTROL;
    case SDLK_RCTRL: return GK_RCONTROL;
    case SDLK_LGUI: return GK_LMETA;
    case SDLK_RGUI: return GK_RMETA;
    default:
        /* keyTyped compares keyBindInventory's, keyBindDrop's and the hotbar
         * bindings' codes: GK_E, GK_Q and GK_1..9 stand for those bindings */
        if (k == SDLK_UNKNOWN) return 0;
        if (k == binds[K_INVENTORY]) return GK_E;
        if (k == binds[K_DROP]) return GK_Q;
        for (int i = 0; i < 9; ++i)
            if (k == binds[K_HOTBAR + i]) return GK_1 + i;
        return 0;
    }
}

/* Minecraft.isRunningOnMac: GuiScreen.isCtrlKeyDown reads command there,
 * control elsewhere (--mac-keys judges a session played on a Mac here) */
#ifdef __APPLE__
static int mac_keys = 1;
#else
static int mac_keys = 0;
#endif

/* Keyboard.isKeyDown for the modifiers, whatever has the input: shift, and
 * GuiScreen.isCtrlKeyDown's keys (command on a Mac, control elsewhere) */
static void modifier_event(const SDL_Event *ev)
{
    if (ev->type != SDL_EVENT_KEY_DOWN && ev->type != SDL_EVENT_KEY_UP) return;
    int lk = lwjgl_key(ev->key.key), down = ev->type == SDL_EVENT_KEY_DOWN;
    int ctrl = mac_keys ? lk == GK_LMETA || lk == GK_RMETA : lk == GK_LCONTROL || lk == GK_RCONTROL;
    if (lk == GK_LSHIFT || lk == GK_RSHIFT || ctrl) gui_input_modifier(&gin, lk, down);
}

/* One event while a container screen is up: the pointer in GuiScreen's
 * scaled pixels, then the screen's own queue. *CLOSE is set by a close key
 * (keyTyped's escape or inventory key closes the screen in this tick). */
static void screen_event(const SDL_Event *ev, int *close)
{
    struct gui_screen_layout layout = gui_input_layout(&gin, cp_gui_container(&CP), CP.sv.potions.size > 0);
    struct gui_event g = {0};
    switch (ev->type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (ev->key.repeat) return;
        g.kind = GE_KEY;
        g.key = lwjgl_key(ev->key.key);
        if (g.key == GK_ESCAPE || g.key == GK_E) *close = 1;
        if (g.key) gui_input_push(&gin, &g);
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        gui_mouse_x = (int)floorf(ev->button.x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev->button.y / (float)(SCALE * layout.scale));
        g.kind = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? GE_PRESS : GE_RELEASE;
        g.button = ev->button.button == SDL_BUTTON_LEFT ? 0 : ev->button.button == SDL_BUTTON_RIGHT ? 1 :
                   ev->button.button == SDL_BUTTON_MIDDLE ? 2 : ev->button.button == SDL_BUTTON_X1 ? 3 : 4;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        gui_mouse_x = (int)floorf(ev->motion.x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev->motion.y / (float)(SCALE * layout.scale));
        g.kind = GE_MOVE;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        /* handleMouseInput: a notch is an event with no button, so a
         * motion to its position (mouseClickMove while a button is held) */
        gui_mouse_x = (int)floorf(ev->wheel.mouse_x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev->wheel.mouse_y / (float)(SCALE * layout.scale));
        g.kind = GE_MOVE;
        break;
    default:
        return;
    }
    g.x = gui_mouse_x;
    g.y = gui_mouse_y;
    gui_input_push(&gin, &g);
}

/* ------------------------------------------------ the chat screen's input */

/* The tick's batch for an open GuiChat or GuiSleepMP, as the window's events
 * build it (GuiScreen.handleInput reads every mouse event, then every key:
 * kept apart and joined at the tick), the tape's ["chat"] op. */
static struct chat_ev chat_mice[CHAT_OP_EV], chat_keys[CHAT_OP_EV];
static int nchat_mice, nchat_keys;
static int chat_paste, chat_copied;   /* a control-V (the batch carries the clipboard), a control-C or X */
static int chat_ptr_moved;          /* the pointer moved since the screen opened */

static void chat_push_key(int code, uint16_t ch)
{
    if (nchat_keys >= CHAT_OP_EV) return;
    chat_keys[nchat_keys++] = (struct chat_ev){.kind = CHAT_EV_KEY, .code = code, .ch = ch};
}

/* a press or release at GUI pixel (gx, gy), in the oracle's display pixels
 * (y from the bottom) that handleMouseInput scales back to it */
static void chat_push_mouse(int kind, int gx, int gy, int button, int d)
{
    if (nchat_mice >= CHAT_OP_EV) return;
    struct chat_ev e = {.kind = (int8_t)kind, .button = (int8_t)button, .code = d};
    e.x = gx * GC_DISPLAY_W / GC_SCREEN_W;
    e.y = (GC_SCREEN_H - 1 - gy) * GC_DISPLAY_H / GC_SCREEN_H;
    chat_mice[nchat_mice++] = e;
}

/* One window event while a chat screen is up: the LWJGL code and char a
 * key press delivers (the text comes as SDL's text input, one event per
 * char with code 0), the mouse's presses, releases and wheel */
static void chat_event(int ctrl, const SDL_Event *ev)
{
    switch (ev->type)
    {
    case SDL_EVENT_KEY_DOWN:
    {
        int code = 0, ch = 0;
        switch (ev->key.key)
        {
        case SDLK_RETURN: code = 28; ch = 13; break;
        case SDLK_KP_ENTER: code = 156; ch = 13; break;
        case SDLK_ESCAPE: code = 1; ch = 27; break;
        case SDLK_BACKSPACE: code = 14; ch = 8; break;
        case SDLK_TAB: code = 15; ch = 9; break;
        case SDLK_DELETE: code = 211; ch = 127; break;
        case SDLK_LEFT: code = 203; break;
        case SDLK_RIGHT: code = 205; break;
        case SDLK_UP: code = 200; break;
        case SDLK_DOWN: code = 208; break;
        case SDLK_HOME: code = 199; break;
        case SDLK_END: code = 207; break;
        case SDLK_PAGEUP: code = 201; break;
        case SDLK_PAGEDOWN: code = 209; break;
        case SDLK_A: if (ctrl) { code = 30; ch = 1; } break;
        case SDLK_C: if (ctrl) { code = 46; ch = 3; chat_copied = 1; } break;
        case SDLK_V: if (ctrl) { code = 47; ch = 22; chat_paste = 1; } break;
        case SDLK_X: if (ctrl) { code = 45; ch = 24; chat_copied = 1; } break;
        default: break;
        }
        if (code) chat_push_key(code, (uint16_t)ch);
        return;
    }
    case SDL_EVENT_TEXT_INPUT:
    {
        if (ctrl) return;
        uint16_t u[64];
        int n = gui_chat_utf16(ev->text.text, u, 64);
        for (int i = 0; i < n; ++i) chat_push_key(0, u[i]);
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        struct gui_screen_layout layout = gui_screen_layout(NULL, W, H, hud_scale);
        gui_mouse_x = (int)floorf(ev->button.x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev->button.y / (float)(SCALE * layout.scale));
        chat_ptr_moved = 1;
        int b = ev->button.button == SDL_BUTTON_LEFT ? 0 : ev->button.button == SDL_BUTTON_RIGHT ? 1 : 2;
        chat_push_mouse(ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? CHAT_EV_PRESS : CHAT_EV_RELEASE,
                        gui_mouse_x, gui_mouse_y, b, 0);
        return;
    }
    case SDL_EVENT_MOUSE_MOTION:
    {
        struct gui_screen_layout layout = gui_screen_layout(NULL, W, H, hud_scale);
        gui_mouse_x = (int)floorf(ev->motion.x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev->motion.y / (float)(SCALE * layout.scale));
        chat_ptr_moved = 1;
        return;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (ev->wheel.y != 0.0f) chat_push_mouse(CHAT_EV_WHEEL, gui_mouse_x, gui_mouse_y, -1, ev->wheel.y > 0 ? 120 : -120);
        return;
    default:
        return;
    }
}

static int live_chat_comp(int px, int py, char *value, size_t nv, char *text, size_t nt);

/* The tick's batch into act a (and the queues emptied): 1 when a key in it
 * closes a GuiChat (Enter, Escape), after which the tick's input block runs */
static int chat_batch(struct act *a, int shift, int ctrl)
{
    int closes = 0;
    if ((CP.screen_chat || CP.screen_sleep) && nchat_mice + nchat_keys > 0)
    {
        struct chat_op *c = &a->chat;
        a->has_chat = 1;
        memset(c, 0, sizeof *c);
        c->mods = (shift ? 1 : 0) | (ctrl ? 2 : 0);
        /* Mouse.getX and getY: the centre the ungrab warped to, until the
         * pointer moves */
        int gx = chat_ptr_moved ? gui_mouse_x : GC_SCREEN_W / 2, gy = chat_ptr_moved ? gui_mouse_y : GC_SCREEN_H / 2 - 1;
        c->px = gx * GC_DISPLAY_W / GC_SCREEN_W;
        c->py = (GC_SCREEN_H - 1 - gy) * GC_DISPLAY_H / GC_SCREEN_H;
        for (int i = 0; i < nchat_mice; ++i) c->ev[c->nev++] = chat_mice[i];
        for (int i = 0; i < nchat_keys && c->nev < CHAT_OP_EV; ++i) c->ev[c->nev++] = chat_keys[i];
        if (chat_paste)
        {
            /* GuiScreen.getClipboardString: the system clipboard */
            char *clip = SDL_GetClipboardText();
            c->has_clip = 1;
            c->clip_len = gui_chat_utf16_allowed(clip ? clip : "", c->clip, GC_MAX);
            SDL_free(clip);
        }
        int left = 0;
        for (int i = 0; i < nchat_mice; ++i)
            if (chat_mice[i].kind == CHAT_EV_PRESS && chat_mice[i].button == 0) left = 1;
        if (left)
            c->has_comp = live_chat_comp(c->px, c->py, c->comp_value, sizeof c->comp_value, c->comp_text,
                                         sizeof c->comp_text);
        for (int i = 0; i < nchat_keys; ++i)
            if (CP.screen_chat && (chat_keys[i].code == 1 || chat_keys[i].code == 28 || chat_keys[i].code == 156))
                closes = 1;
    }
    nchat_mice = nchat_keys = 0;
    chat_paste = 0;
    return closes;
}

/* After the tick: a copy or a cut set the system clipboard
 * (GuiScreen.setClipboardString) */
static void chat_after_tick(void)
{
    if (chat_copied)
    {
        char u[GC_MAX * 3 + 1];
        gui_chat_utf8(CP.chat_clip.s, CP.chat_clip.n, u);
        SDL_SetClipboardText(u);
    }
    chat_copied = 0;
}

/* the window pointer the script's last press, release or motion left (a
 * frame's camera motion is made there) */
static float script_px, script_py;

/* An input script line as the SDL event the window would receive (the
 * --input-script driver and the GUI judge); 0 for a line that is none.
 * Besides keys, presses and motion: {"wheel":D} a wheel notch, {"focus":0|1}
 * the window losing or taking the focus. */
static int script_event(const struct jval *row, SDL_Event *e)
{
    memset(e, 0, sizeof *e);
    int64_t n = 0;
    if (json_int(json_get(row, "wheel"), &n))
    {
        e->type = SDL_EVENT_MOUSE_WHEEL;
        e->wheel.y = (float)n;
        int64_t wx = 0, wy = 0;
        if (json_int(json_get(row, "x"), &wx) && json_int(json_get(row, "y"), &wy))
        {
            struct gui_screen_layout gl = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
            script_px = (float)(wx * gl.scale * SCALE + 1);
            script_py = (float)(wy * gl.scale * SCALE + 1);
        }
        e->wheel.mouse_x = script_px;
        e->wheel.mouse_y = script_py;
        return 1;
    }
    if (json_int(json_get(row, "focus"), &n))
    {
        e->type = n ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
        return 1;
    }
    const char *key = json_str(json_get(row, "key"));
    int64_t value = 0;
    if (key)
    {
        json_int(json_get(row, "down"), &value);
        e->type = value ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e->key.key = script_key(key);
        int64_t rep = 0;
        json_int(json_get(row, "repeat"), &rep);
        e->key.repeat = rep != 0;   /* the key held: the system's autorepeat */
        return 1;
    }
    struct gui_screen_layout gl = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
    int64_t x = 0, y = 0, button = 0;
    json_int(json_get(row, "x"), &x);
    json_int(json_get(row, "y"), &y);
    if (json_get(row, "mouse"))
    {
        json_int(json_get(row, "mouse"), &button);
        json_int(json_get(row, "down"), &value);
        e->type = value ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e->button.button = button == 1 ? SDL_BUTTON_LEFT : button == 2 ? SDL_BUTTON_RIGHT :
                           button == 4 ? SDL_BUTTON_X1 : button == 5 ? SDL_BUTTON_X2 : SDL_BUTTON_MIDDLE;
        e->button.x = script_px = (float)(x * gl.scale * SCALE + 1);
        e->button.y = script_py = (float)(y * gl.scale * SCALE + 1);
        return 1;
    }
    if (json_get(row, "motion"))
    {
        int64_t dx = 0, dy = 0;
        json_int(json_get(row, "dx"), &dx);
        json_int(json_get(row, "dy"), &dy);
        e->type = SDL_EVENT_MOUSE_MOTION;
        e->motion.x = script_px = (float)(x * gl.scale * SCALE + 1);
        e->motion.y = script_py = (float)(y * gl.scale * SCALE + 1);
        e->motion.xrel = (float)dx;   /* the grabbed mouse's look, in window pixels */
        e->motion.yrel = (float)dy;
        return 1;
    }
    if (json_get(row, "wheel"))
    {
        int64_t n = 0;
        json_int(json_get(row, "wheel"), &n);
        e->type = SDL_EVENT_MOUSE_WHEEL;
        e->wheel.y = (float)n;
        return 1;
    }
    return 0;
}

/* The synthetic driver posts the same SDL events as the window receives. A
 * line is one event at tick t; coordinates are GuiScreen scaled pixels:
 *   {"t":T,"key":"w","down":1}   w a s d q e r 1-9, space shift ctrl escape, or any SDL key name
 *                                ("repeat":1: an autorepeat press)
 *   {"t":T,"mouse":1,"x":X,"y":Y,"down":1}   1 left, 2 right, 3 middle, 4 and 5 the side buttons
 *   {"t":T,"motion":1,"x":X,"y":Y,"dx":DX,"dy":DY}   the pointer on a screen, DX DY the grabbed look
 *   {"t":T,"wheel":N}   {"t":T,"look":[YAW,PITCH]}   {"t":T,"quit":1}
 * (csrc/tests/playfuzz/gen.c, out/native/playfuzz_gen, writes them from a model of a player) */
static SDL_Keycode script_key(const char *s)
{
    if (!s) return SDLK_UNKNOWN;
    if (!strcmp(s, "escape")) return SDLK_ESCAPE;
    if (!strcmp(s, "shift")) return SDLK_LSHIFT;
    if (!strcmp(s, "ctrl")) return SDLK_LCTRL;
    if (!strcmp(s, "space")) return SDLK_SPACE;
    /* any other name is SDL's own (SDL_GetKeyName: "Left Ctrl", "Tab", "F5") */
    return SDL_GetKeyFromName(s);
}

static struct jval *script_next(FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    struct jval *j = NULL;
    while (getline(&line, &cap, f) >= 0)
    {
        if (line[0] == '#' || line[0] == '\n') continue;
        j = json_parse(strdup(line));
        break;
    }
    free(line);
    return j;
}

/* --check REF: a Java recording from this snapshot (its own tape.jsonl, or
 * a check.sh replay) through this client's own tick pair, headless. Every
 * row after the snapshot is compared on the players' positions, the world
 * fields and the server digests (test_snapshots.c's server set); the first
 * difference is named. The gate replays tapes through the test harness; this
 * is how the product's own server path is checked against the same rows. */
static int row_double(const struct jval *o, const char *k, double *out)
{
    uint64_t bits;
    if (!json_double(json_get(o, k), &bits)) return 0;
    memcpy(out, &bits, 8);
    return 1;
}

static int check_row(const char *path, const struct jval *row, int64_t t);
/* the checked tape records every row's S02s (header "s02") */
static int check_s02;

/* DIR/clientview.jsonl (tests/clientview.sh: the oracle's render state at
 * each frame): what this client draws from its own copies against the
 * oracle client's, after the tick pair of the frame's tick: WorldClient's
 * clock, each chest's lid and previous lid, each spawner's spin and delay,
 * each mob copy's equipment. 0
 * when they agree (or the frame has no tick here), else the first
 * difference is printed. */
static int check_clientview(const char *path, const struct jval *v, int64_t t)
{
    int64_t wt;
    if (json_int(json_get(v, "wt"), &wt) && wt != CP.cw_day)
    {
        printf("FAIL play check %s: row %lld client world time want %lld got %lld\n", path, (long long)t,
               (long long)wt, (long long)CP.cw_day);
        return 3;
    }
    const struct jval *ch = json_get(v, "chests");
    for (int i = 0; i < json_len(ch); ++i)
    {
        const struct jval *c = json_at(ch, i);
        int64_t x = 0, y = 0, z = 0;
        uint32_t lid = 0, plid = 0;
        json_int(json_at(c, 0), &x);
        json_int(json_at(c, 1), &y);
        json_int(json_at(c, 2), &z);
        json_float(json_at(c, 3), &lid);
        json_float(json_at(c, 4), &plid);
        const struct clid *cl = chest_lid_of((int)x, (int)y, (int)z);
        uint32_t gl = 0, gp = 0;
        if (cl) { memcpy(&gl, &cl->lid, 4); memcpy(&gp, &cl->prev, 4); }
        if (gl != lid || gp != plid)
        {
            printf("FAIL play check %s: row %lld chest %lld,%lld,%lld lid want %08x/%08x got %08x/%08x\n", path,
                   (long long)t, (long long)x, (long long)y, (long long)z, lid, plid, gl, gp);
            return 3;
        }
    }
    const struct jval *sps = json_get(v, "spawners");
    for (int i = 0; i < json_len(sps); ++i)
    {
        const struct jval *c = json_at(sps, i);
        int64_t x = 0, y = 0, z = 0, delay = 0;
        uint64_t rot = 0, prot = 0;
        json_int(json_at(c, 0), &x);
        json_int(json_at(c, 1), &y);
        json_int(json_at(c, 2), &z);
        json_double(json_at(c, 3), &rot);
        json_double(json_at(c, 4), &prot);
        json_int(json_at(c, 5), &delay);
        const struct spin *s = NULL;
        for (int k = 0; k < nspins && !s; ++k)
            if (spins[k].x == x && spins[k].y == y && spins[k].z == z) s = &spins[k];
        uint64_t gr = 0, gp = 0;
        if (s) { memcpy(&gr, &s->rot, 8); memcpy(&gp, &s->prev, 8); }
        if (!s || gr != rot || gp != prot || s->delay != delay)
        {
            printf("FAIL play check %s: row %lld spawner %lld,%lld,%lld rot want %016llx/%016llx delay %lld got %016llx/%016llx delay %d\n",
                   path, (long long)t, (long long)x, (long long)y, (long long)z, (unsigned long long)rot,
                   (unsigned long long)prot, (long long)delay, (unsigned long long)gr, (unsigned long long)gp, s ? s->delay : -1);
            return 3;
        }
    }
    const struct jval *bl = json_get(v, "blasts");
    if (bl)
    {
        int k = 0, nfx = particles_live_count(&surv_fx);
        for (int i = 0; i < nfx; ++i)
        {
            const struct live_fx *f = particles_live_get(&surv_fx, i);
            if (f->dead || (f->kind != PLIVE_HUGE_EXPLODE && f->kind != PLIVE_LARGE_EXPLODE)) continue;
            const struct jval *b = k < json_len(bl) ? json_at(bl, k) : NULL;
            int64_t kind = 0, age = -1;
            uint64_t bx = 0, by = 0, bz = 0, gx, gy, gz;
            if (b)
            {
                json_int(json_at(b, 0), &kind);
                json_double(json_at(b, 1), &bx);
                json_double(json_at(b, 2), &by);
                json_double(json_at(b, 3), &bz);
                json_int(json_at(b, 4), &age);
            }
            memcpy(&gx, &f->e.pos_x, 8);
            memcpy(&gy, &f->e.pos_y, 8);
            memcpy(&gz, &f->e.pos_z, 8);
            int gk = f->kind == PLIVE_HUGE_EXPLODE ? 1 : 2;
            if (!b || kind != gk || bx != gx || by != gy || bz != gz || age != f->age)
            {
                printf("FAIL play check %s: row %lld explosion particle %d want %s got kind %d at %.17g %.17g %.17g age %d\n",
                       path, (long long)t, k, b ? "another" : "none", gk, f->e.pos_x, f->e.pos_y, f->e.pos_z, f->age);
                return 3;
            }
            ++k;
        }
        if (k != json_len(bl))
        {
            printf("FAIL play check %s: row %lld explosion particles want %d got %d\n", path, (long long)t, json_len(bl), k);
            return 3;
        }
    }
    /* the client's items and orbs (its copies), and the S0D's pickups */
    const struct jval *its = json_get(v, "items");
    if (its)
    {
        int ncopies = 0;
        for (int i = 0; i < ncitems; ++i) ncopies += citems[i].has_copy;
        for (int i = 0; i < json_len(its); ++i)
        {
            const struct jval *it = json_at(its, i);
            uint64_t x = 0, y = 0, z = 0;
            json_double(json_at(it, 0), &x);
            json_double(json_at(it, 1), &y);
            json_double(json_at(it, 2), &z);
            int found = 0;
            for (int k = 0; k < ncitems && !found; ++k)
            {
                uint64_t cx, cy, cz;
                memcpy(&cx, &citems[k].e.pos_x, 8);
                memcpy(&cy, &citems[k].e.pos_y, 8);
                memcpy(&cz, &citems[k].e.pos_z, 8);
                found = citems[k].has_copy && cx == x && cy == y && cz == z;
            }
            if (!found)
            {
                printf("FAIL play check %s: row %lld the client's item %d has no copy here\n", path, (long long)t, i);
                return 3;
            }
        }
        if (ncopies != json_len(its))
        {
            printf("FAIL play check %s: row %lld client items want %d got %d\n", path, (long long)t, json_len(its), ncopies);
            return 3;
        }
    }
    const struct jval *pks = json_get(v, "pickups");
    if (pks)
    {
        int k = 0;
        for (int i = 0; i < npickups; ++i)
        {
            if (pickups[i].resting) continue;
            const struct jval *pk = k < json_len(pks) ? json_at(pks, k) : NULL;
            uint64_t x = 0, y = 0, z = 0, gx, gy, gz;
            int64_t age = -1;
            if (pk)
            {
                json_double(json_at(pk, 0), &x);
                json_double(json_at(pk, 1), &y);
                json_double(json_at(pk, 2), &z);
                json_int(json_at(pk, 3), &age);
            }
            memcpy(&gx, &pickups[i].x, 8);
            memcpy(&gy, &pickups[i].y, 8);
            memcpy(&gz, &pickups[i].z, 8);
            if (!pk || gx != x || gy != y || gz != z || age != pickups[i].age)
            {
                printf("FAIL play check %s: row %lld pickup %d want %s got %.17g %.17g %.17g age %d\n", path,
                       (long long)t, k, pk ? "another" : "none", pickups[i].x, pickups[i].y, pickups[i].z,
                       pickups[i].age);
                return 3;
            }
            ++k;
        }
        if (k != json_len(pks))
        {
            printf("FAIL play check %s: row %lld pickups want %d got %d\n", path, (long long)t, json_len(pks), k);
            return 3;
        }
    }
    const struct jval *ls = json_get(v, "leash");
    for (int i = 0; SS.server_rows && i < json_len(ls); ++i)
    {
        const struct jval *m = json_at(ls, i);
        int64_t id = 0, want = -1;
        json_int(json_at(m, 0), &id);
        json_int(json_at(m, 1), &want);
        const struct living *l = cent_server_living((int)id);
        const struct cent_living *cl = cent_find((int)id);
        if (!l) continue;
        int lh = cl ? cl->leash_holder : l->leash_holder;
        uint64_t lk = cl ? cl->leash_knot : l->leash_knot;
        int64_t got = -1;
        if (lh == LEASH_PLAYER) got = SR.player_entity_id;
        else if (lh == LEASH_KNOT && lk)
        {
            uint32_t ki = (uint32_t)lk;
            const fh_ent *kn = ((lk >> 32) & 0xFFFFu) == (nw_arena()->fh_ents.gen[ki] & 0xFFFFu) ? fh_ent_at((int32_t)ki) : NULL;
            got = kn ? kn->entity_id : -2;
        }
        if (got != want)
        {
            printf("FAIL play check %s: row %lld entity %lld lead holder want %lld got %lld\n", path, (long long)t,
                   (long long)id, (long long)want, (long long)got);
            return 3;
        }
    }
    const struct jval *eq = json_get(v, "equip");
    for (int i = 0; SS.server_rows && i < json_len(eq); ++i)
    {
        const struct jval *m = json_at(eq, i);
        int64_t id = 0;
        json_int(json_at(m, 0), &id);
        const struct living *l = cent_server_living((int)id);
        const struct cent_living *cl = cent_find((int)id);
        if (!l) continue;
        for (int k = 0; k < 5; ++k)
        {
            int64_t want = 0;
            json_int(json_at(json_at(m, 1), k), &want);
            const struct equip_slot *sl = cl ? &cl->equip[k] : &l->equip[k];
            int got = sl->count > 0 ? sl->id : 0;
            if (got == want) continue;
            printf("FAIL play check %s: row %lld entity %lld equipment slot %d want %lld got %d\n", path, (long long)t,
                   (long long)id, k, (long long)want, got);
            return 3;
        }
    }
    return 0;
}

static int check_rows(const char *path, const char *dir)
{
    /* the oracle client's view, when the recording carries one */
    FILE *cvf = NULL;
    struct jval *cv = NULL;
    {
        char cvp[1024];
        snprintf(cvp, sizeof cvp, "%s/clientview.jsonl", dir);
        cvf = fopen(cvp, "rb");
    }
    long cv_frames = 0;
    struct tape tp;
    if (!tape_open(&tp, path)) { fprintf(stderr, "play: cannot read %s\n", path); return 1; }
    check_s02 = json_get(tp.hdr, "s02") != NULL;
    const struct jval *row;
    long rows = 0;
    int rc;
    /* dead livings are freed between rows, as the replay gate does (grave.h) */
    grave_enable(GRAVE_THRESHOLD);
    while ((rc = tape_next(&tp, &row)) == 1)
    {
        int64_t t;
        if (!json_int(json_get(row, "t"), &t) || t < S.tick) continue;
        struct act a;
        char why[160];
        if (session_parse_act(row, &a, why, sizeof why))
        {
            printf("FAIL play check %s: row %lld: %s\n", path, (long long)t, why);
            tape_close(&tp);
            return 3;
        }
        play_tick(t, &a);
        ++rows;
        if (check_row(path, row, t)) { tape_close(&tp); return 3; }
        /* the oracle client's frame after this tick */
        int64_t vt = -1;
        while (cvf && (cv || (cv = script_next(cvf))) && json_int(json_get(cv, "t"), &vt) && vt <= t)
        {
            int bad = vt == t ? check_clientview(path, cv, t) : 0;
            cv_frames += vt == t;
            json_free(cv);
            cv = NULL;
            if (bad) { tape_close(&tp); fclose(cvf); return 3; }
        }
    }
    json_free(cv);
    if (cvf) fclose(cvf);
    if (cvf) printf("play check %s: %ld client-view frames agree (clock, chest lids, spawners, equipment, leads, explosion particles, items, pickups)\n", path, cv_frames);
    tape_close(&tp);
    if (rc < 0) { printf("FAIL play check %s: a row does not parse\n", path); return 3; }
    if (rows == 0) { printf("FAIL play check %s: no row after the snapshot\n", path); return 3; }
    printf("PASS play check %s: %ld rows through play's own tick pair, 0 mismatches\n", path, rows);
    return 0;
}

/* One row's recorded state against the tick just run: the players'
 * positions, the world fields and the server digests. 0 when they agree,
 * else the first difference is printed. */
static int check_row(const char *path, const struct jval *row, int64_t t)
{
    {
        /* a movement snapshot's rows carry the two players only */
        const struct jval *cpj = json_get(row, "cp"), *spj = json_get(row, "sp");
        const struct jval *wj = json_get(row, "w"), *dj = json_get(row, "d");
        if (!cpj || !spj || (SS.server_rows && (!wj || !dj)))
        {
            printf("FAIL play check %s: row %lld carries no cp, sp, w or d\n", path, (long long)t);
            return 3;
        }
        static const char *pos[6] = {"cp.x", "cp.y", "cp.z", "sp.x", "sp.y", "sp.z"};
        double pgot[6] = {CP.e.pos_x, CP.e.pos_y, CP.e.pos_z, SP.e.pos_x, SP.e.pos_y, SP.e.pos_z};
        for (int i = 0; i < 6; ++i)
        {
            double want;
            if (!row_double(i < 3 ? cpj : spj, pos[i] + 3, &want) || want == pgot[i]) continue;
            printf("FAIL play check %s: row %lld %s want %.17g got %.17g\n", path, (long long)t, pos[i],
                   want, pgot[i]);
            return 3;
        }
        if (!SS.server_rows) return 0;
        /* the S02 chat lines the server tick sent, on tapes that carry them */
        if (check_s02)
        {
            char why[512];
            if (chat_s02_check(row, s2c_sent_queue(), why, sizeof why))
            {
                printf("FAIL play check %s: row %lld %s\n", path, (long long)t, why);
                return 3;
            }
        }
        static const char *wf[4] = {"wt", "tt", "bc", "ents"};
        int64_t wgot[4] = {sr_world_time(&SR), sr_total_time(&SR), sr_block_writes(&SR),
                           S.players - (SP.sv.removed || SR.player_unlisted ? 1 : 0) + SR.native_entities};
        for (int i = 0; i < 4; ++i)
        {
            int64_t want;
            if (!json_int(json_get(wj, wf[i]), &want) || want == wgot[i]) continue;
            printf("FAIL play check %s: row %lld w.%s want %lld got %lld\n", path, (long long)t, wf[i],
                   (long long)want, (long long)wgot[i]);
            return 3;
        }
        static const char *df[6] = {"blk", "sw", "sseed", "smath", "sstat", "ents"};
        uint64_t dgot[6];
        dgot[0] = sr_block_hash(&SR);
        dgot[1] = sr_world_rng(&SR);
        sr_det(&SR, &dgot[2], &dgot[3], &dgot[4]);
        dgot[5] = sr_entity_digest(&S, &SR, &SP);
        for (int i = 0; i < 6; ++i)
        {
            const char *hex = json_str(json_get(dj, df[i]));
            if (!hex || strtoull(hex, NULL, 16) == dgot[i]) continue;
            printf("FAIL play check %s: row %lld d.%s want %s got %016llx\n", path, (long long)t, df[i], hex,
                   (unsigned long long)dgot[i]);
            return 3;
        }
    }
    return 0;
}

/* A row's screen ops as text, in the order the tick applies them. A drag's
 * slots (mode 5, stage 1) are one set: Java sends them in its HashSet's
 * order, which the result does not depend on, so a run of them is sorted. */
static void ops_text(const struct act *a, char *buf, size_t n)
{
    size_t k = 0;
    int nc = 0;
    buf[0] = 0;
    for (int i = 0; i < a->clicks && k + 64 < n; )
    {
        /* a closed screen's clicks after their close */
        for (; nc < a->closes && a->close_at[nc] <= i && k + 16 < n; ++nc)
            k += (size_t)snprintf(buf + k, n - k, "[close]");
        int j = i;
        const struct guiclick *c = &a->gui_click[i];
        if (c->mode == 5 && (c->button & 3) == 1)
        {
            int slots[CONTAINER_MAX_SLOTS + 2], m = 0;
            while (j < a->clicks && a->gui_click[j].mode == 5 && a->gui_click[j].button == c->button)
                slots[m++] = a->gui_click[j++].slot;
            for (int x = 1; x < m; ++x)
                for (int y = x; y > 0 && slots[y - 1] > slots[y]; --y)
                { int tmp = slots[y]; slots[y] = slots[y - 1]; slots[y - 1] = tmp; }
            for (int x = 0; x < m && k + 64 < n; ++x)
                k += (size_t)snprintf(buf + k, n - k, "[click %d %d %d 5]", c->window, slots[x], c->button);
            i = j;
            continue;
        }
        k += (size_t)snprintf(buf + k, n - k, "[click %d %d %d %d]", c->window, c->slot, c->button, c->mode);
        ++i;
    }
    for (int i = 0; i < a->trsels && i < 8 && k + 64 < n; ++i)
        k += (size_t)snprintf(buf + k, n - k, "[trsel %d %d]", a->trsel[i].window, a->trsel[i].index);
    if (a->gui_close && !a->closes && k + 16 < n) k += (size_t)snprintf(buf + k, n - k, "[close]");
    for (; nc < a->closes && k + 16 < n; ++nc) k += (size_t)snprintf(buf + k, n - k, "[close]");
}

/* RAW's events of tick T (an input script's mouse, motion and key lines)
 * through the SDL path the window's events take, into the screen's queue;
 * *EV is the next unread line. The number fed. */
static long feed_raw(FILE *rf, struct jval **ev, int64_t t)
{
    long n = 0;
    while (*ev)
    {
        int64_t et = -1;
        json_int(json_get(*ev, "t"), &et);
        if (et > t) break;
        SDL_Event e;
        int close = 0;
        if (et == t && script_event(*ev, &e))
        {
            modifier_event(&e);
            if (CP.screen_inventory) screen_event(&e, &close);
            ++n;
        }
        json_free(*ev);
        *ev = script_next(rf);
    }
    return n;
}

/* --gui-judge REF --gui-raw RAW: the GUI input judge's native half. REF is
 * a recording the oracle made while it fed RAW's raw events (an input
 * script: mouse presses, releases and motion in GuiScreen pixels, keys) to
 * Java's own GuiContainer through LWJGL's queues. Each row runs with REF's
 * act, its screen ops taken out, and RAW's events of that tick through the
 * SDL path the window's events take (script_event, modifier_event,
 * screen_event) into this client's screen handlers; a frame is taken to be
 * drawn after each tick, as the headless input script draws one. The ops
 * the tick made must be REF's, and every row's state REF's. */
static int judge_gui(const char *ref, const char *raw)
{
    struct tape tp;
    if (!tape_open(&tp, ref)) { fprintf(stderr, "play: cannot read %s\n", ref); return 1; }
    FILE *rf = fopen(raw, "r");
    if (!rf) { fprintf(stderr, "play: cannot read %s\n", raw); tape_close(&tp); return 1; }
    struct jval *ev = script_next(rf);
    const struct jval *row;
    long rows = 0, ops_ticks = 0, events = 0;
    int rc;
    grave_enable((size_t)96 << 20);
    static char want[16384], got[16384];
    while ((rc = tape_next(&tp, &row)) == 1)
    {
        int64_t t;
        if (!json_int(json_get(row, "t"), &t) || t < S.tick) continue;
        struct act a;
        char why[160];
        if (session_parse_act(row, &a, why, sizeof why))
        {
            printf("FAIL gui judge %s: row %lld: %s\n", ref, (long long)t, why);
            break;
        }
        ops_text(&a, want, sizeof want);
        a.clicks = 0;
        a.trsels = 0;
        a.gui_close = a.closes = 0;
        a.gui_in = &gin;
        events += feed_raw(rf, &ev, t);
        play_tick(t, &a);
        gui_input_drawn(&gin, &CP);
        gui_input_frame(&gin);
        ++rows;
        ops_text(&a, got, sizeof got);
        if (strcmp(want, got))
        {
            printf("FAIL gui judge %s: row %lld: Java's screen sent %s, this client's %s\n", ref, (long long)t,
                   want[0] ? want : "nothing", got[0] ? got : "nothing");
            rc = -2;
            break;
        }
        ops_ticks += want[0] != 0;
        if (check_row(ref, row, t)) { rc = -2; break; }
    }
    tape_close(&tp);
    if (ev) json_free(ev);
    fclose(rf);
    if (rc == -2) return 3;
    if (rc < 0) { printf("FAIL gui judge %s: a row does not parse\n", ref); return 3; }
    if (ops_ticks == 0) { printf("FAIL gui judge %s: no screen op in %ld rows\n", ref, rows); return 3; }
    printf("PASS gui judge %s: %ld rows, %ld raw events, %ld ticks of screen ops as Java's GuiContainer sent them\n",
           ref, rows, events, ops_ticks);
    return 0;
}

/* --shot-golden PNG [--shot-budget "R G B N"]: the shot against the
 * oracle's frame of the same tick, measured as test_render measures a scene
 * (each channel's mean absolute error over the frame, and the pixels whose
 * largest channel error is over 25). With a budget (what the frame measured
 * when it was accepted, a ratchet as tests/render_budget.txt's lines are) a
 * frame worse than it fails: a dropped item not drawn or a screen's slots
 * drawn empty add their pixels. 0 on a pass. */
static int shot_compare(const unsigned char *img, const char *golden, const char *budget, int tick, float pt)
{
    int gw = 0, gh = 0;
    unsigned char *g = png_read_rgba(golden, &gw, &gh);
    if (!g || gw != W || gh != H)
    {
        printf("FAIL frame judge t=%d: %s is not a %dx%d PNG\n", tick, golden, W, H);
        free(g);
        return 1;
    }
    double sum[3] = {0, 0, 0};
    long px25 = 0, n = (long)W * H;
    for (long i = 0; i < n; ++i)
    {
        int worst = 0;
        for (int c = 0; c < 3; ++c)
        {
            int d = abs((int)img[i * 3 + c] - (int)g[i * 4 + c]);
            sum[c] += d;
            if (d > worst) worst = d;
        }
        if (worst > 25) ++px25;
    }
    free(g);
    double mean[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    printf("frame t=%d pt=%.2f: mean %.6f %.6f %.6f /ch, %ld px over 25 of %ld against %s\n", tick, (double)pt,
           mean[0], mean[1], mean[2], px25, n, golden);
    if (!budget) return 0;
    double b[3];
    long bpx;
    if (sscanf(budget, "%lf %lf %lf %ld", &b[0], &b[1], &b[2], &bpx) != 4)
    {
        printf("FAIL frame judge t=%d: budget \"%s\" is not R G B N\n", tick, budget);
        return 1;
    }
    int bad = px25 > bpx;
    for (int c = 0; c < 3; ++c) bad |= mean[c] > b[c] + 1e-6;
    printf("%s frame judge t=%d pt=%.2f %.6f %.6f %.6f /ch, %ld px %s its budget %.6f %.6f %.6f /ch, %ld px\n",
           bad ? "FAIL" : "PASS", tick, (double)pt, mean[0], mean[1], mean[2], px25,
           bad ? "is worse than" : "is within", b[0], b[1], b[2], bpx);
    return bad;
}

/* The shot's world passes after the terrain frame: the entities, the tile
 * entities, the block overlays, the particles, the weather, the hand and the
 * overlays (LOG prints what they drew; DEPTH_OUT, when set, takes the depth
 * before the hand's pass clears it). */
static void shot_passes(unsigned char *img, float shot_pt, int aim, const char *assets, int log, float *depth_out,
                        struct rs_in *inp, struct rs_out *op)
{
    struct rs_in in;
    struct rs_out o;
    build_rs(&in, shot_pt);
    rs_compute(&in, &o);
    float *open = NULL;
    if (aim)
    {
        open = malloc((size_t)W * H * sizeof *open);
        for (size_t k = 0; k < (size_t)W * H; ++k) open[k] = 1.0F;
    }
    int ne = draw_hitboxes(img, &o, shot_pt, open, assets);
    if (log) fprintf(stderr, "play: %d entities drawn\n", ne);
    int nt = draw_tileents(img, &o, shot_pt, sr_total_time(&SR), assets);
    if (log) fprintf(stderr, "play: %d tile entities drawn\n", nt);
    draw_block_overlays(img, &o);
    if (log) fprintf(stderr, "play: hand equipped %.2f (prev %.2f), swing %.3f, item %d:%d, use %d\n",
            equip.eq, equip.peq, CP.swing_progress, equip.present ? equip.item : -1,
            equip.damage, CP.sv.using_slot >= 0 ? CP.sv.using_count : 0);
    int dx = 0, dy = 0, dz = 0;
    int stage = surv_client_dig_stage(&dx, &dy, &dz);
    if (!log) {}
    else if (surv_mo.cur.num)
        fprintf(stderr, "play: selected block %d,%d,%d\n",
                surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);
    else fprintf(stderr, "play: no selected block\n");
    if (log) fprintf(stderr, "play: damage stage %d at %d,%d,%d\n", stage, dx, dy, dz);
    free(open);
    /* the same particle pass the live loop draws */
    surv_fx.light_world = view_world();
    particles_live_light(&surv_fx);
    {
        int aw = 0, ah = 0;
        const unsigned char *tex[4] = {raster_live_particle_tex(RL),
                                       raster_live_block_atlas(RL, &aw, &ah),
                                       NULL, raster_live_explosion_tex(RL)};
        const int tex_wh[8] = {128, 128, aw, ah, 256, 256, 128, 128};
        raster_particles_draw_live(particles_live_get(&surv_fx, 0), particles_live_count(&surv_fx),
                                   W, H, o.proj, o.mv, (const double[]){o.camx, o.camy, o.camz},
                                   o.gfogc, o.gfogs, o.gfoge, o.gfogd, o.gfogm,
                                   (const uint32_t *)o.lm,
                                   raster_live_depth_mut(RL), img, NULL, NULL,
                                   tex, tex_wh, raster_live_mesher(RL),
                                   shot_pt, pyaw + (yaw - pyaw) * shot_pt, ppitch + (pitch - ppitch) * shot_pt);
    }
    /* the translucent terrain over them all */
    raster_live_translucent(RL, img);
    /* the world's depth before the hand pass clears it */
    if (depth_out) memcpy(depth_out, raster_live_depth_mut(RL), (size_t)W * H * sizeof *depth_out);
    *inp = in;
    *op = o;
}

/* The shot's world passes, then the weather, the hand and the overlays
 * over them (live_worldfx). */
static void shot_world(unsigned char *img, float shot_pt, int aim, const char *assets, int log, float *depth_out)
{
    struct rs_in in;
    struct rs_out o;
    shot_passes(img, shot_pt, aim, assets, log, depth_out, &in, &o);
    live_worldfx(&in, &o, shot_pt, img, 1);
}

/* --shot-items FILE: the frame judge's presence check. FILE lists the
 * items and orbs the oracle's own frame drew at each tick ({"t", "k", "x",
 * "y", "z"}: the client world's copies, EntityRenderProbe's visible
 * entities). Each must have something of this client's drops drawn within
 * R pixels of where it projects (one the world hides there is skipped,
 * DEPTH0 the depth of the frame without the drops): IMG and BARE are the
 * frame with and without the drops. The live world draws the server's items, which rest up
 * to a few tenths of a block from the client's copies (the tracker's
 * quantized positions), so the check is a neighbourhood, not a pixel.
 * 0 when every one is there. */
/* The live world draws the server's drops, which rest up to a few tenths
 * of a block from the client's copies, up to two blocks while they fall
 * or fly: one of this client's own drops of the kind that near P whose
 * centre the world hides (DEPTH0)
 * or the frame leaves out is there, only behind the corner (or past the
 * edge) the oracle's copy clears; one drawn around its own centre is
 * drawn (its spin is the server item's, so a flat one can be edge-on where
 * the oracle's faces the camera). */
static int native_drop_hidden(const struct rs_out *o, const float *depth0, const double p[3], int orb,
                              const unsigned char *img, const unsigned char *bare, int radius, long need)
{
    for (int j = 0; j < ncitems; ++j)
    {
        const struct citem *it = &citems[j];
        if (!it->has_copy || it->kind != (orb ? IE_ORB : IE_ITEM)) continue;
        /* this client's copy near the oracle's, or the copy of the server
         * entity near it: a copy strays from the server's between the
         * tracker's updates (every 20 ticks for both kinds), and the copy's
         * own push and bounce come from its own Random */
        double dx = it->e.pos_x - p[0], dy = it->e.pos_y - p[1], dz = it->e.pos_z - p[2];
        int near = dx * dx + dy * dy + dz * dz <= 4.0;
        for (int l = 0; l < 2 && !near; ++l)
        {
            const ie_world *iw = drop_lists(l);
            for (int k = 0; iw && k < iw->n && !near; ++k)
            {
                const ie_ent *se = ie_ent_at(iw->slot[k]);
                if (!se || se->is_dead || se->entity_id != it->id) continue;
                double sx = se->e.pos_x - p[0], sy = se->e.pos_y - p[1], sz = se->e.pos_z - p[2];
                near = sx * sx + sy * sy + sz * sz <= 4.0;
            }
        }
        if (!near) continue;
        double w[3] = {it->e.pos_x - o->camx, it->e.pos_y + (orb ? 0.075 : 0.2) - o->camy, it->e.pos_z - o->camz}, eye[4], clip[4];
        for (int r = 0; r < 4; ++r) eye[r] = o->mv[r] * w[0] + o->mv[4 + r] * w[1] + o->mv[8 + r] * w[2] + o->mv[12 + r];
        for (int r = 0; r < 4; ++r)
            clip[r] = o->proj[r] * eye[0] + o->proj[4 + r] * eye[1] + o->proj[8 + r] * eye[2] + o->proj[12 + r] * eye[3];
        if (clip[3] <= 0.05) continue;
        int sx = (int)((clip[0] / clip[3] + 1.0) * 0.5 * W), sy = (int)((1.0 - clip[1] / clip[3]) * 0.5 * H);
        float z = (float)(clip[2] / clip[3] * 0.5 + 0.5);
        if (sx < 0 || sy < 0 || sx >= W || sy >= H) return 1;   /* off the frame here */
        if (depth0[(size_t)sy * W + (size_t)sx] <= z) return 1;
        /* drawn where this client has it (its own spin, its own spot) */
        long drawn = 0;
        for (int y = sy - radius; y <= sy + radius; ++y)
            for (int x = sx - radius; x <= sx + radius; ++x)
            {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                size_t q = ((size_t)y * W + (size_t)x) * 3;
                drawn += img[q] != bare[q] || img[q + 1] != bare[q + 1] || img[q + 2] != bare[q + 2];
            }
        if (drawn >= need) return 1;
    }
    return 0;
}

static int shot_presence(const unsigned char *img, const unsigned char *bare, const float *depth0, const char *path,
                         int tick, float pt)
{
    enum { R = 48 };
    FILE *f = fopen(path, "r");
    if (!f) { printf("FAIL frame judge t=%d: cannot read %s\n", tick, path); return 1; }
    struct rs_in in;
    struct rs_out o;
    build_rs(&in, pt);
    rs_compute(&in, &o);
    char *line = NULL;
    size_t cap = 0;
    int n = 0, missing = 0;
    while (getline(&line, &cap, f) > 0)
    {
        struct jval *j = json_parse(strdup(line));
        int64_t t = -1;
        if (!j || !json_int(json_get(j, "t"), &t) || t != tick) { json_free(j); continue; }
        double p[3];
        const char *k = json_str(json_get(j, "k"));
        int ok = k != NULL;
        static const char *axis[3] = {"x", "y", "z"};
        for (int a = 0; a < 3 && ok; ++a)
        {
            uint64_t bits;
            ok = json_double(json_get(j, axis[a]), &bits);
            memcpy(&p[a], &bits, 8);
        }
        char kind[16];
        snprintf(kind, sizeof kind, "%s", ok ? k : "?");
        json_free(j);
        if (!ok) continue;
        double w[3] = {p[0] - o.camx, p[1] + 0.2 - o.camy, p[2] - o.camz}, eye[4], clip[4];
        for (int r = 0; r < 4; ++r) eye[r] = o.mv[r] * w[0] + o.mv[4 + r] * w[1] + o.mv[8 + r] * w[2] + o.mv[12 + r];
        for (int r = 0; r < 4; ++r)
            clip[r] = o.proj[r] * eye[0] + o.proj[4 + r] * eye[1] + o.proj[8 + r] * eye[2] + o.proj[12 + r] * eye[3];
        if (clip[3] <= 0.05) continue;
        int sx = (int)((clip[0] / clip[3] + 1.0) * 0.5 * W), sy = (int)((1.0 - clip[1] / clip[3]) * 0.5 * H);
        if (sx < -R || sx >= W + R || sy < -R || sy >= H + R) continue;
        /* behind the world where it projects (window z): the oracle's
         * frame hides it too */
        float z = (float)(clip[2] / clip[3] * 0.5 + 0.5);
        if (sx >= 0 && sy >= 0 && sx < W && sy < H && depth0[(size_t)sy * W + (size_t)sx] <= z) continue;
        long drawn = 0;
        for (int y = sy - R; y <= sy + R; ++y)
            for (int x = sx - R; x <= sx + R; ++x)
            {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                size_t q = ((size_t)y * W + (size_t)x) * 3;
                drawn += img[q] != bare[q] || img[q + 1] != bare[q + 1] || img[q + 2] != bare[q + 2];
            }
        ++n;
        /* a far drop covers a few pixels: its own projected size (an item's
         * icon a quarter block across, an orb's a tenth) caps the count
         * asked for */
        double across = (strcmp(kind, "orb") ? 0.25 : 0.1) * o.proj[5] / clip[3] * 0.5 * H;
        /* under a pixel across it may cover no pixel's centre on either side
         * (at 854x480 no drop the oracle draws is: its render range, 32
         * blocks for an orb and 16 for an item, keeps it a pixel or more) */
        if (across < 1.0)
        {
            printf("frame judge t=%d: the oracle's %s at %.2f %.2f %.2f is under a pixel across here\n",
                   tick, kind, p[0], p[1], p[2]);
            continue;
        }
        long need = across * across < 16.0 ? (long)(across * across) : 16;
        if (need < 1) need = 1;
        if (drawn < need && native_drop_hidden(&o, depth0, p, !strcmp(kind, "orb"), img, bare, R, need))
        {
            printf("frame judge t=%d: the oracle's %s at %.2f %.2f %.2f: this client's own copy is drawn, hidden or off the frame\n",
                   tick, kind, p[0], p[1], p[2]);
            continue;
        }
        if (drawn < need)
        {
            printf("FAIL frame judge t=%d: the oracle's %s at %.2f %.2f %.2f (screen %d,%d) has no drop drawn within %d px\n",
                   tick, kind, p[0], p[1], p[2], sx, sy, R);
            ++missing;
        }
    }
    free(line);
    fclose(f);
    printf("%s frame judge t=%d pt=%.2f: %d of the oracle's %d visible drops drawn\n", missing ? "FAIL" : "PASS", tick,
           (double)pt, n - missing, n);
    return missing != 0;
}

/* --shots FILE (the frame judge): several shots over one replay. Each line
 * "TICK PT [R G B N [drawn]]" is a shot drawn after TICK at partial tick
 * PT as the live loop draws its frame (the terrain frame, the world's
 * passes, the HUD, the screens, the toast), measured against the oracle's
 * frame of that tick at that partial tick (--shot-golden-dir DIR:
 * f_TTTTTT.png at 1.0, f_TTTTTT.pNNN.png at 0.NNN, the frame at 1.0 when
 * the recording has none at that partial tick) and, with R G B N,
 * judged against that budget; --shot-items checks its drops; --shot-dir
 * DIR writes each as s_TTTTTT.pNNN.png. */
struct judge_shot { int tick; float pt; char budget[288]; int drawn; };

/* EntityRenderer.updateCameraAndRender's screen mouse for the pointer at the
 * window's centre, where the agent frames have it (LWJGL's y from the
 * bottom): x * width / displayWidth, height - y * height / displayHeight - 1 */
static int centre_mouse_x(const struct gui_screen_layout *gl)
{
    return (W / 2) * gl->sw / W;
}

/* LWJGL reports the grabbed pointer's y one row off the centre until the
 * first frame drawn after the session's first ungrab (setIngameNotInFocus's
 * setCursorPosition) has read the warp's motion event: that frame (both its
 * partial ticks) sees the screen mouse one row lower. An agent-mode oracle
 * polls only in the frames it draws (Display.update is in the render
 * block), so with the shots the frames it drew, the settling frame is the
 * first shot at or after the first tick a screen was up, whether or not one
 * is up in it (a replay notes each tick, shot_note_tick). */
static int pointer_settle_tick = -1;
static int first_ungrab_tick = -1;
/* GuiInventory's stored pointer (the preview's gaze) is the last frame's
 * that drew the same screen: each opening of the inventory is a new screen
 * (inv_gen), and the shots remember which one they last drew */
static int inv_gen, inv_gen_open, shot_inv_gen = -1;

static int screen_ungrabbed(void)
{
    return CP.screen_inventory || CP.screen_chat || CP.screen_gameover || CP.screen_sleep || CP.screen_credits;
}

static void shot_note_tick(int tick)
{
    if (first_ungrab_tick < 0 && screen_ungrabbed()) first_ungrab_tick = tick;
    const struct container *oc = cp_gui_container(&CP);
    int inv = CP.screen_inventory && (!oc || oc->kind == CONTAINER_PLAYER);
    if (inv && !inv_gen_open) ++inv_gen;
    inv_gen_open = inv;
}

static void pointer_note_shot(int tick)
{
    shot_note_tick(tick);
    if (pointer_settle_tick < 0 && first_ungrab_tick >= 0 && tick >= first_ungrab_tick) pointer_settle_tick = tick;
}

/* A shot draws the open inventory: a screen no earlier shot drew starts
 * with GuiInventory's stored pointer at 0, 0 (--shot-screen-drawn or a
 * shot's "drawn": the oracle drew it in a frame the shots do not have) */
static void shot_inv_drawn(int drawn)
{
    if (!inv_gen_open) return;
    if (!drawn && shot_inv_gen != inv_gen) inv_was_open = 0;
    shot_inv_gen = inv_gen;
}

static int pointer_shot_tick = -1;

static int centre_mouse_y(const struct gui_screen_layout *gl)
{
    return gl->sh - (H / 2) * gl->sh / H - 1 + (pointer_shot_tick >= 0 && pointer_shot_tick == pointer_settle_tick);
}

static void shot_golden_name(char *buf, size_t n, const char *dir, int tick, float pt)
{
    snprintf(buf, n, "%s/f_%06d.p%03d.png", dir, tick, (int)lroundf(pt * 1000.0F));
    FILE *f = pt < 1.0F ? fopen(buf, "rb") : NULL;
    if (f) fclose(f);
    else snprintf(buf, n, "%s/f_%06d.png", dir, tick);
}

/* --obs-dump DIR: each shot's world passes as the device renderer's input
 * (raster_obs.h: the terrain's lists, and every pass after the opaque
 * terrain as raster_rec recorded it) and the C frame of them before the
 * HUD and the screens, DIR/o_TTTTTT.pNNN.obs */
static const char *obs_dump_dir;
static struct raster_rec *obs_rec;

static int obs_dump(const unsigned char *frame, int tick, float pt)
{
    struct raster_obs o;
    char path[1024];
    snprintf(path, sizeof path, "%s/o_%06d.p%03d.obs", obs_dump_dir, tick, (int)lroundf(pt * 1000.0F));
    int rc = raster_live_obs(RL, &o);
    if (!rc)
    {
        raster_rec_attach(obs_rec, &o);
        rc = raster_obs_write(path, &o, frame);
    }
    if (rc) fprintf(stderr, "play: cannot write %s\n", path);
    return rc;
}

/* The judges' device frames (framedev.h): the plugin, its renderer (NULL
 * until the first shot opens it, or when there is none), --frames (0 auto,
 * 1 the C renderer, 2 the device or fail) and --frames-check N (every Nth
 * shot, from the first, is drawn by the C renderer too and compared). */
static const struct framedev_api *fdev;
static void *fdev_r;
/* the frames of one render: FDEV_ENVS environments, two a shot with
 * --shot-items (the frame and the frame without the drops) */
enum { FDEV_ENVS = 8, FDEV_MIN_SHOTS = 8 };
static int frames_mode, frames_check = 16, fdev_tried, fdev_shots, fdev_checked, fdev_bad;
static double fdev_ms;
static struct raster_rec *fdev_rec[FDEV_ENVS];   /* one a frame of the render */

static void fdev_close(const char *why)
{
    if (fdev_r) fdev->close(fdev_r);
    fdev_r = NULL;
    if (why) fprintf(stderr, "play: the device renderer stopped (%s); the C renderer draws the rest\n", why);
}

/* the C renderer draws the rest: the device's frames cost more (WHY),
 * which the plugin notes for the judges that start soon after */
static void fdev_slow(const char *why)
{
    if (fdev && fdev_r) fdev->slow(fdev_r, why);
    fdev_close(why);
}

/* The plugin found beside this program and its renderer opened, on a
 * thread of its own while the replay runs up to the first shot (creating
 * the device's context takes most of a second under the other lanes'
 * load): fdev_start begins it, fdev_ready takes its answer. */
static char fdev_why[256];
#if defined(__linux__)
static _Atomic int fdev_opened;   /* fdev_open_run has its answer */
static void *fdev_open_run(void *arg)
{
    (void)arg;
    snprintf(fdev_why, sizeof fdev_why, "not built (make -C csrc gpu-render-judge)");
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 64);
    if (n <= 0) return NULL;
    path[n] = 0;
    char *slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, 64, "cuda/libframedev.so");
    if (access(path, R_OK)) return NULL;
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    const struct framedev_api *(*get)(void) = NULL;
    if (h) *(void **)&get = dlsym(h, "framedev_api");
    else snprintf(fdev_why, sizeof fdev_why, "%.200s", dlerror());
    const struct framedev_api *api = get ? get() : NULL;
    if (api && api->abi != FRAMEDEV_ABI)
    {
        snprintf(fdev_why, sizeof fdev_why, "the plugin is ABI %d, play %d (make -C csrc gpu-render-judge)", api->abi, FRAMEDEV_ABI);
        api = NULL;
    }
    if (!api) return NULL;
    const struct framedev_sizes z = {sizeof(struct raster_obs), sizeof(struct raster_obs_draw),
                                     sizeof(struct raster_obs_sky), sizeof(struct raster_obs_cmd),
                                     sizeof(struct raster_obs_estate), sizeof(struct raster_obs_equad),
                                     sizeof(struct raster_obs_tex), sizeof(struct raster_obs_crack),
                                     sizeof(struct raster_obs_portal), sizeof(struct raster_obs_line)};
    fdev = api;
    fdev_r = api->open(FDEV_ENVS, W, H, &z, frames_mode == 2, fdev_why, sizeof fdev_why);
    atomic_store(&fdev_opened, 1);
    return NULL;
}

static void *fdev_open_thread(void *arg)
{
    fdev_open_run(arg);
    grave_hold(0);
    return NULL;
}

static pthread_t fdev_thread;
static int fdev_thread_on;
#endif

static void fdev_start(void)
{
    if (fdev_tried || frames_mode == 1) return;
    fdev_tried = 1;
    struct raster_obs probe;
    if (raster_live_obs(RL, &probe)) { snprintf(fdev_why, sizeof fdev_why, "mipmapped textures"); return; }
#if defined(__linux__)
    /* no sweep while the open maps and unmaps memory (grave.h grave_hold):
     * the thread ends the hold when the open returns */
    grave_hold(1);
    fdev_thread_on = !pthread_create(&fdev_thread, NULL, fdev_open_thread, NULL);
    if (!fdev_thread_on) fdev_open_thread(NULL);
#else
    snprintf(fdev_why, sizeof fdev_why, "not Linux");
#endif
}

/* the shots are done: an open still running is waited for (the process
 * must not end under it), and the renderer closed */
static void fdev_finish(void)
{
#if defined(__linux__)
    if (fdev_thread_on) pthread_join(fdev_thread, NULL);
    fdev_thread_on = 0;
#endif
    fdev_close(NULL);
}

/* the renderer at a shot: 1 when the device draws, 0 when the C renderer
 * does, -1 when --frames device finds no device */
static int fdev_ready(void)
{
    fdev_start();
#if defined(__linux__)
    if (fdev_thread_on)
    {
        /* the shots before the device is open are the C renderer's (the
         * same bytes), unless --frames device asks for the device's */
        if (!atomic_load(&fdev_opened) && frames_mode != 2) return 0;
        pthread_join(fdev_thread, NULL);
        fdev_thread_on = 0;
        if (!fdev_r)
        {
            fprintf(stderr, "play: the C renderer draws the shots: no device renderer (%s)\n", fdev_why);
            if (frames_mode == 2) return -1;
        }
        else
        {
            for (int e = 0; e < FDEV_ENVS; ++e) fdev_rec[e] = play_rec_new();
            fprintf(stderr, "play: the device draws the shots' world frames (every %d%s checked against the C renderer)\n",
                    frames_check, frames_check == 1 ? "st" : frames_check == 2 ? "nd" : frames_check == 3 ? "rd" : "th");
        }
    }
#endif
    if (fdev_r) return 1;
    return frames_mode == 2 ? -1 : 0;
}

/* A and B (frames and depth buffers) the same bytes: 0, else FAIL lines */
static int fdev_same(const char *what, int tick, float pt, const unsigned char *dev, const unsigned char *c,
                     const float *ddev, const float *dc)
{
    long n = 0, nd = 0, first = -1, firstd = -1;
    for (long i = 0; i < (long)W * H; ++i)
    {
        if (memcmp(dev + i * 3, c + i * 3, 3)) { if (first < 0) first = i; ++n; }
        if (memcmp(ddev + i, dc + i, sizeof *dc)) { if (firstd < 0) firstd = i; ++nd; }
    }
    if (!n && !nd)
    {
        printf("PASS device frame t=%d pt=%.2f: the %s and its world depth equal the C renderer's\n", tick, (double)pt, what);
        return 0;
    }
    if (n)
    {
        const unsigned char *a = dev + first * 3, *b = c + first * 3;
        printf("FAIL device frame t=%d pt=%.2f: the %s differs from the C renderer's in %ld px, first %ld,%ld "
               "device %d %d %d C %d %d %d\n", tick, (double)pt, what, n, first % W, first / W, a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    if (nd)
        printf("FAIL device frame t=%d pt=%.2f: the %s's world depth differs from the C renderer's in %ld px, first %ld,%ld "
               "device %.9g C %.9g\n", tick, (double)pt, what, nd, firstd % W, firstd / W, (double)ddev[firstd], (double)dc[firstd]);
    return 1;
}

/* --shot-items: whether the oracle drew any drop at TICK (FILE lists
 * them by tick; 1 when FILE cannot be read, so the check reports it). A
 * shot with none needs no frame without the drops: the check finds
 * nothing to look for. */
static int shot_tick_has_drops(const char *path, int tick)
{
    static const char *loaded;
    static int *ticks, nticks, unreadable;
    if (loaded != path)
    {
        loaded = path;
        free(ticks);
        ticks = NULL;
        nticks = 0;
        FILE *f = fopen(path, "r");
        unreadable = !f;
        char *line = NULL;
        size_t cap = 0;
        while (f && getline(&line, &cap, f) > 0)
        {
            struct jval *j = json_parse(strdup(line));
            int64_t t;
            if (j && json_int(json_get(j, "t"), &t))
            {
                ticks = realloc(ticks, (size_t)(nticks + 1) * sizeof *ticks);
                ticks[nticks++] = (int)t;
            }
            json_free(j);
        }
        free(line);
        if (f) fclose(f);
    }
    if (unreadable) return 1;
    for (int i = 0; i < nticks; ++i)
        if (ticks[i] == tick) return 1;
    return 0;
}

/* renderParticles leaves EntityFX.interpPos at a frame's camera for the
 * next: the recording's oracle drew exactly these shots, in this order
 * (--frame-ticks, --frame-pts) */
static void shot_fx_interp(float spt)
{
    fx_interp[0] = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * spt;
    fx_interp[1] = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * spt;
    fx_interp[2] = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * spt;
    fx_interp_set = 1;
}

/* one shot's world frames on the way through the device */
struct fdev_shot {
    const struct judge_shot *js;
    int check, nenv;
    unsigned char *frame[2], *c[2];
    float *depth[2], *cdepth[2];
    struct raster_obs ob[2];
    struct raster_obs_draw *draws[2];   /* the obs's own copies of the lists */
    int32_t *orders[2];
    struct rs_in in;
    struct rs_out o;
};

/* O's draw lists and orders (raster_live_obs's buffers, which the next
 * frame reuses) copied into buffers of its own */
static void fdev_own_lists(struct raster_obs *o, struct raster_obs_draw **dp, int32_t **op)
{
    int n = o->nopaque + o->nwater;
    size_t norder = 0;
    for (int i = 0; i < o->nwater; ++i) norder += (size_t)o->water[i].count / 4;
    struct raster_obs_draw *d = malloc((size_t)(n ? n : 1) * sizeof *d);
    int32_t *ord = malloc((norder ? norder : 1) * sizeof *ord);
    memcpy(d, o->opaque, (size_t)n * sizeof *d);   /* opaque then water, one array */
    size_t at = 0;
    for (int i = o->nopaque; i < n; ++i)
        if (d[i].order)
        {
            memcpy(ord + at, d[i].order, (size_t)(d[i].count / 4) * sizeof *ord);
            d[i].order = ord + at;
            at += (size_t)(d[i].count / 4);
        }
    o->opaque = d;
    o->water = d + o->nopaque;
    *dp = d;
    *op = ord;
}

/* One shot's world passes recorded (drawn by the C renderer too on a
 * checked shot), its frame and, with ITEMS, its frame without the drops:
 * the terrain, the entities and everything after them up to the
 * translucent sections, which is what the device draws. */
static void fdev_record(struct fdev_shot *f, struct raster_rec *const *rec, const char *items, int aim,
                        const char *assets)
{
    float pt = f->js->pt;
    size_t npx = (size_t)W * H;
    frame_now_ms = ((int64_t)f->js->tick + 1) * 50;
    f->check = fdev_shots++ % frames_check == 0;
    f->nenv = items && shot_tick_has_drops(items, f->js->tick) ? 2 : 1;
    for (int e = 0; e < f->nenv; ++e)
    {
        f->frame[e] = malloc(npx * 3);
        f->depth[e] = malloc(npx * sizeof(float));
    }
    struct rs_in in;
    struct rs_out o;
    build_rs(&in, pt);
    rs_compute(&in, &o);
    struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day,
                                CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt, play_clouds};
    unsigned char *world0 = NULL;
    float *depth0 = NULL;
    raster_rec_begin(rec[0]);
    raster_rec_set_only(!f->check);
    raster_live_frame(RL, &li, f->frame[0]);
    if (f->check && f->nenv > 1)
    {
        world0 = malloc(npx * 3);
        depth0 = malloc(npx * sizeof *depth0);
        memcpy(world0, f->frame[0], npx * 3);
        memcpy(depth0, raster_live_depth_mut(RL), npx * sizeof *depth0);
    }
    for (int e = 0; e < f->nenv; ++e)
    {
        if (e)
        {
            /* the same terrain, the passes again without the drops */
            raster_rec_begin(rec[e]);
            if (f->check)
            {
                memcpy(f->frame[1], world0, npx * 3);
                memcpy(raster_live_depth_mut(RL), depth0, npx * sizeof *depth0);
            }
            shot_skip_drops = 1;
        }
        shot_passes(f->frame[e], pt, aim, assets, 0, NULL, &f->in, &f->o);
        shot_skip_drops = 0;
        /* the world's depth, before the weather, the hand and the overlays
         * (the drops check's) */
        if (f->check)
        {
            f->cdepth[e] = malloc(npx * sizeof(float));
            memcpy(f->cdepth[e], raster_live_depth_mut(RL), npx * sizeof(float));
        }
        raster_rec_marker(RASTER_CMD_DEPTH_SNAP);
        live_worldfx(&f->in, &f->o, pt, f->frame[e], 1);
        raster_rec_end();
        if (e == f->nenv - 1) raster_rec_set_only(0);
        raster_live_obs(RL, &f->ob[e]);
        raster_rec_attach(rec[e], &f->ob[e]);
        fdev_own_lists(&f->ob[e], &f->draws[e], &f->orders[e]);
        if (f->check)
        {
            f->c[e] = malloc(npx * 3);
            memcpy(f->c[e], f->frame[e], npx * 3);
        }
    }
    free(world0);
    free(depth0);
}

static void fdev_shot_free(struct fdev_shot *f)
{
    for (int e = 0; e < 2; ++e)
    {
        free(f->frame[e]); free(f->c[e]); free(f->depth[e]); free(f->cdepth[e]);
        free(f->draws[e]); free(f->orders[e]);
    }
    memset(f, 0, sizeof *f);
}

/* The device is shared: when its frames cost this client more than the C
 * renderer's (the recording and the render against the recording and
 * drawing of the checked shots, once six frames were drawn on the device,
 * with a check past the first shot's meshing when there is one), the C
 * renderer draws the rest (the same bytes). A run of fewer than
 * FDEV_MIN_SHOTS shots does not open the device at all. */
static double fdev_dev_ms, fdev_c_ms, fdev_c0_ms;
static int fdev_dev_envs, fdev_c_envs, fdev_c0_envs, fdev_renders;

static void fdev_policy(void)
{
    if (!fdev_r || frames_mode == 2 || fdev_renders < 2 || fdev_dev_envs < 6 || !(fdev_c_envs || fdev_c0_envs)) return;
    double dev = fdev_dev_ms / fdev_dev_envs, c = fdev_c_envs ? fdev_c_ms / fdev_c_envs : fdev_c0_ms / fdev_c0_envs;
    if (dev <= c) return;
    char why[160];
    snprintf(why, sizeof why, "the shared device's frames cost %.1f ms here, the C renderer's %.1f", dev, c);
    fdev_slow(why);
}

/* The shots JS[0..N) that follow one tick, their world frames on the
 * device in one render (FDEV_ENVS frames at most: more shots take more
 * renders), each shot then finished in C in order (judge_shot_finish).
 * The C renderer draws a shot when the device fails before it drew it.
 * The first nonzero judgement (or 0) is returned. */
static int judge_shot_finish(const struct judge_shot *js, unsigned char *img, const char *golden_dir,
                             const char *shot_dir);
static int judge_shot_draw(const struct judge_shot *js, const char *golden_dir, const char *shot_dir,
                           const char *items, int aim, const char *assets);

static int judge_tick_device(struct judge_shot *const *js, int n, const char *golden_dir, const char *shot_dir,
                             const char *items, int aim, const char *assets)
{
    int per = FDEV_ENVS / (items ? 2 : 1), rc = 0;
    struct fdev_shot f[FDEV_ENVS];
    for (int s0 = 0; s0 < n; s0 += per)
    {
        int m = n - s0 < per ? n - s0 : per, nenv = 0;
        memset(f, 0, sizeof f);
        /* the recordings point into the terrain's meshes: none is dropped
         * until the render has read them */
        raster_live_keep(RL, 1);
        double rec_ms = 0;   /* the record-only shots' recording */
        int rec_envs = 0;
        for (int i = 0; i < m; ++i)
        {
            f[i].js = js[s0 + i];
            Uint64 t0 = SDL_GetTicksNS();
            fdev_record(&f[i], fdev_rec + nenv, items, aim, assets);
            double t = (double)(SDL_GetTicksNS() - t0) / 1e6;
            shot_fx_interp(f[i].js->pt);
            nenv += f[i].nenv;
            if (f[i].check)
            {
                /* the C renderer drew these too: what a frame costs it */
                if (fdev_shots == 1) { fdev_c0_ms = t; fdev_c0_envs = f[i].nenv; }
                else { fdev_c_ms += t; fdev_c_envs += f[i].nenv; }
            }
            else { rec_ms += t; rec_envs += f[i].nenv; }
        }
        Uint64 t1 = SDL_GetTicksNS();
        int bad = !fdev_r;
        for (int i = 0, e = 0; i < m && !bad; ++i)
            for (int k = 0; k < f[i].nenv && !bad; ++k) bad = fdev->set(fdev_r, e++, &f[i].ob[k]) != 0;
        double ms = 0;
        bad = bad || fdev->render(fdev_r, nenv, &ms) != 0;
        unsigned char *rgbs[FDEV_ENVS];
        float *depths[FDEV_ENVS];
        for (int i = 0, e = 0; i < m; ++i)
            for (int k = 0; k < f[i].nenv; ++k, ++e) { rgbs[e] = f[i].frame[k]; depths[e] = f[i].depth[k]; }
        bad = bad || fdev->download(fdev_r, nenv, rgbs, depths) != 0;
        raster_live_keep(RL, 0);
        if (bad && fdev_r) fdev_close("a device call failed");
        if (!bad)
        {
            fdev_ms += ms;
            ++fdev_renders;
            if (rec_envs)
            {
                fdev_dev_ms += rec_ms + (double)(SDL_GetTicksNS() - t1) / 1e6 * rec_envs / nenv;
                fdev_dev_envs += rec_envs;
            }
            fdev_policy();
        }
        for (int i = 0; i < m; ++i)
        {
            struct fdev_shot *s = &f[i];
            const struct judge_shot *j = s->js;
            int r = 0;
            if (bad && !s->check)
            {
                /* nothing drawn: the C renderer draws the shot */
                r = judge_shot_draw(j, golden_dir, shot_dir, items, aim, assets);
                if (r && !rc) rc = r;
                fdev_shot_free(s);
                continue;
            }
            size_t npx = (size_t)W * H;
            if (bad)
                for (int k = 0; k < s->nenv; ++k)
                {
                    memcpy(s->frame[k], s->c[k], npx * 3);
                    memcpy(s->depth[k], s->cdepth[k], npx * sizeof(float));
                }
            else if (s->check)
            {
                ++fdev_checked;
                for (int k = 0; k < s->nenv; ++k)
                    if (fdev_same(k ? "frame without the drops" : "frame", j->tick, j->pt, s->frame[k], s->c[k], s->depth[k],
                                  s->cdepth[k]))
                    {
                        ++fdev_bad;
                        r = 3;
                        /* the reference's frame is judged */
                        memcpy(s->frame[k], s->c[k], npx * 3);
                        memcpy(s->depth[k], s->cdepth[k], npx * sizeof(float));
                    }
            }
            frame_now_ms = ((int64_t)j->tick + 1) * 50;
            int items_rc = 0;
            if (items && shot_presence(s->frame[0], s->nenv > 1 ? s->frame[1] : NULL, s->nenv > 1 ? s->depth[1] : NULL,
                                       items, j->tick, j->pt))
                items_rc = 3;
            int fr = judge_shot_finish(j, s->frame[0], golden_dir, shot_dir);
            r = fr ? fr : items_rc ? items_rc : r;
            if (r && !rc) rc = r;
            fdev_shot_free(s);
        }
    }
    return rc;
}

/* One shot's frame drawn by the C renderer (the terrain frame, the
 * world's passes, the weather, the hand and the overlays), then finished
 * (judge_shot_finish); with --shot-items its drops checked. */
static int judge_shot_draw(const struct judge_shot *js, const char *golden_dir, const char *shot_dir,
                           const char *items, int aim, const char *assets)
{
    float pt = js->pt;
    frame_now_ms = ((int64_t)js->tick + 1) * 50;
    unsigned char *img = malloc((size_t)W * H * 3);
    {
        struct rs_in in;
        struct rs_out o;
        build_rs(&in, pt);
        rs_compute(&in, &o);
        struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day,
                                    CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt, play_clouds};
        raster_live_frame(RL, &li, img);
    }
    int items_rc = 0;
    unsigned char *world0 = NULL, *bare = NULL;
    float *depth0 = NULL;
    /* no drop to look for: no frame without the drops */
    const char *all_items = items;
    if (items && !shot_tick_has_drops(items, js->tick)) items = NULL;
    if (items)
    {
        world0 = malloc((size_t)W * H * 3);
        bare = malloc((size_t)W * H * 3);
        depth0 = malloc((size_t)W * H * sizeof *depth0);
        memcpy(world0, img, (size_t)W * H * 3);
        memcpy(depth0, raster_live_depth_mut(RL), (size_t)W * H * sizeof *depth0);
    }
    if (obs_dump_dir)
    {
        if (!obs_rec) obs_rec = play_rec_new();
        raster_rec_begin(obs_rec);
    }
    shot_world(img, pt, aim, assets, 0, NULL);
    if (obs_dump_dir)
    {
        raster_rec_end();
        int r = obs_dump(img, js->tick, pt);
        /* without goldens the dump is the shot */
        if (r || !golden_dir)
        {
            free(img); free(world0); free(bare); free(depth0);
            return r ? 1 : 0;
        }
    }
    if (items)
    {
        memcpy(bare, world0, (size_t)W * H * 3);
        memcpy(raster_live_depth_mut(RL), depth0, (size_t)W * H * sizeof *depth0);
        shot_skip_drops = 1;
        shot_world(bare, pt, aim, assets, 0, depth0);
        shot_skip_drops = 0;
        if (shot_presence(img, bare, depth0, items, js->tick, pt)) items_rc = 3;
        free(world0); free(bare); free(depth0);
    }
    else if (all_items && shot_presence(img, NULL, NULL, all_items, js->tick, pt)) items_rc = 3;
    int rc = judge_shot_finish(js, img, golden_dir, shot_dir);
    free(img);
    return rc ? rc : items_rc;
}

/* The shot's frame over its world: the HUD, the screens and the toast as
 * the live frame draws them, then measured against the oracle's frame. */
static int judge_shot_finish(const struct judge_shot *js, unsigned char *img, const char *golden_dir,
                             const char *shot_dir)
{
    float pt = js->pt;
    live_hud(img, js->tick);
    {
        struct gui_screen_layout gl = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
        pointer_note_shot(js->tick);
        pointer_shot_tick = js->tick;
        shot_inv_drawn(js->drawn);
        if (js->drawn && CP.screen_inventory)
        {
            inv_was_open = 1;
            inv_mouse_x = (float)centre_mouse_x(&gl);
            inv_mouse_y = (float)centre_mouse_y(&gl);
        }
        live_screens(img, 0, centre_mouse_x(&gl), centre_mouse_y(&gl));
    }
    live_toast(img, js->tick + 1);   /* getSystemTime after the tick: the next tick * 50 */
    int rc = 0;
    char path[1024];
    if (shot_dir)
    {
        char name[64];
        snprintf(name, sizeof name, "s_%06d.p%03d.png", js->tick, (int)lroundf(pt * 1000.0F));
        snprintf(path, sizeof path, "%s/%s", shot_dir, name);
        rc = raster_png(path, W, H, img);
    }
    shot_golden_name(path, sizeof path, golden_dir, js->tick, pt);
    if (!rc && shot_compare(img, path, js->budget[0] ? js->budget : NULL, js->tick, pt)) rc = 3;
    return rc;
}

/* The live loop's input state between frames: what the window's events have
 * left for the next tick (the keys, the wheel, the screens' requests) and
 * the mouse's motion for the next frame's camera. */
struct live
{
    /* the settings the F-keys set, and whether the next row carries them */
    int opt_tpv, opt_hide, opt_dbg, opt_smooth, opts_changed;
    SDL_Window *win;
    struct key_state keys;
    int cmd_down, ctrl_down, wheel, respawn, wake, close_screen, paused, grabbed, running, scripted;
    float mdx, mdy;
    double tick_at;
};

/* One window event, as the live loop takes it between ticks. */
static void live_event(struct live *L, const SDL_Event *evp)
{
    SDL_Event ev = *evp;
    switch (ev.type)
    {
    case SDL_EVENT_QUIT: L->running = 0; break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        int down = ev.type == SDL_EVENT_KEY_DOWN;
        SDL_Keycode k = ev.key.key;
        if (k == SDLK_LGUI || k == SDLK_RGUI) L->cmd_down = down;
        /* GuiScreen.isCtrlKeyDown: command on a Mac, control elsewhere */
        if (mac_keys ? k == SDLK_LGUI || k == SDLK_RGUI : k == SDLK_LCTRL || k == SDLK_RCTRL) L->ctrl_down = down;
        modifier_event(&ev);
        if (down && k == SDLK_Q && L->cmd_down) { L->running = 0; break; }
        /* a container screen takes every key (GuiScreen.handleInput
         * runs before the game's own keyboard loop) */
        if (CP.screen_inventory) { screen_event(&ev, &L->close_screen); break; }
        /* so does a chat screen (GuiChat, GuiSleepMP, whose escape is the
         * Leave Bed button): its key presses, repeats included */
        if (CP.screen_chat || CP.screen_sleep) { if (down) chat_event(L->ctrl_down, &ev); break; }
        /* a held key's autorepeat: Keyboard.next skips it with repeat
         * events off (in game): no binding, no F-key, no pause */
        if (ev.key.repeat) break;
        if (down && k == SDLK_ESCAPE && CP.screen_gameover) break;
        /* GuiWinGame.keyTyped: escape skips the credits (the C16) */
        if (down && !ev.key.repeat && k == SDLK_ESCAPE && CP.screen_credits) { L->respawn = 1; break; }
        if (down && k == SDLK_ESCAPE && L->paused)
        {
            L->paused = 0;
            L->grabbed = 1;
            SDL_SetWindowRelativeMouseMode(L->win, true);
            L->tick_at = (double)SDL_GetTicksNS() / 1e9;
            break;
        }
        if (down && k == SDLK_ESCAPE && !L->paused)
        {
            L->paused = 1;
            L->grabbed = 0;
            SDL_SetWindowRelativeMouseMode(L->win, false);
            memset(&L->keys, 0, sizeof L->keys);   /* setIngameNotInFocus: KeyBinding.unPressAllKeys */
            break;
        }
        /* Minecraft.runTick's keyboard loop with no screen up: F1 hideGUI,
         * F3 showDebugInfo, F5 (keyBindTogglePerspective) the view, F8
         * (keyBindSmoothCamera) the cinematic camera */
        if (down && !ev.key.repeat && L->grabbed && !L->paused)
        {
            if (k == SDLK_F1) L->opt_hide ^= 1, L->opts_changed = 1;
            if (k == SDLK_F3) L->opt_dbg ^= 1, L->opts_changed = 1;
            if (k == binds[K_PERSPECTIVE]) L->opt_tpv = L->opt_tpv >= 2 ? 0 : L->opt_tpv + 1, L->opts_changed = 1;
            if (k == SDLK_F8) L->opt_smooth ^= 1, L->opts_changed = 1;
        }
        int b = L->grabbed && !L->paused ? key_for(k) : -1;
        if (b < 0) break;
        /* keyBindTogglePerspective: its press is taken in the same key
         * event (runTick's isPressed), only its held state is left */
        if (down && !ev.key.repeat && b != K_PERSPECTIVE) ++L->keys.presses[b];
        L->keys.held[b] = (uint8_t)down;
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (CP.screen_chat || CP.screen_sleep) chat_event(L->ctrl_down, &ev);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        int down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (CP.screen_inventory) { screen_event(&ev, &L->close_screen); break; }
        if (CP.screen_chat || CP.screen_sleep) { chat_event(L->ctrl_down, &ev); break; }
        struct gui_screen_layout layout = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
        gui_mouse_x = (int)floorf(ev.button.x / (float)(SCALE * layout.scale));
        gui_mouse_y = (int)floorf(ev.button.y / (float)(SCALE * layout.scale));
        if (CP.screen_gameover)
        {
            if (down && ev.button.button == SDL_BUTTON_LEFT &&
                gui_mouse_x >= layout.sw / 2 - 100 && gui_mouse_x < layout.sw / 2 + 100 &&
                gui_mouse_y >= layout.sh / 4 + 72 && gui_mouse_y < layout.sh / 4 + 92 &&
                death_screen_ticks >= 20)
                L->respawn = 1;
            break;
        }
        if (L->paused)
        {
            if (down && ev.button.button == SDL_BUTTON_LEFT &&
                gui_mouse_x >= layout.sw / 2 - 100 && gui_mouse_x < layout.sw / 2 + 100 &&
                gui_mouse_y >= layout.sh / 4 + 24 && gui_mouse_y < layout.sh / 4 + 44)
            {
                L->paused = 0;
                L->grabbed = 1;
                SDL_SetWindowRelativeMouseMode(L->win, true);
                L->tick_at = (double)SDL_GetTicksNS() / 1e9;
            }
            break;
        }
        if (!L->grabbed)
        {
            if (down) { L->grabbed = 1; SDL_SetWindowRelativeMouseMode(L->win, true); }
            break;
        }
        int b = ev.button.button == SDL_BUTTON_LEFT ? K_ATTACK :
                ev.button.button == SDL_BUTTON_RIGHT ? K_USE :
                ev.button.button == SDL_BUTTON_MIDDLE ? K_PICK : -1;
        if (b < 0) break;
        if (down) ++L->keys.presses[b];
        L->keys.held[b] = (uint8_t)down;
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (CP.screen_inventory) screen_event(&ev, &L->close_screen);
        else if (CP.screen_chat || CP.screen_sleep) chat_event(L->ctrl_down, &ev);
        else if (CP.screen_gameover || L->paused)
        {
            struct gui_screen_layout layout = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
            gui_mouse_x = (int)floorf(ev.motion.x / (float)(SCALE * layout.scale));
            gui_mouse_y = (int)floorf(ev.motion.y / (float)(SCALE * layout.scale));
        }
        else if (L->grabbed) { L->mdx += ev.motion.xrel; L->mdy += ev.motion.yrel; }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (CP.screen_inventory) screen_event(&ev, &L->close_screen);
        else if (CP.screen_chat || CP.screen_sleep) chat_event(L->ctrl_down, &ev);
        else if (L->grabbed) L->wheel += ev.wheel.y > 0 ? 1 : ev.wheel.y < 0 ? -1 : 0;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (L->scripted) break;
        L->grabbed = 0;
        SDL_SetWindowRelativeMouseMode(L->win, false);
        memset(L->keys.held, 0, sizeof L->keys.held);
        break;
    default: break;
    }
}

/* --judge REF, with a frame-structured --input-script (csrc/play/rawjudge.sh):
 * REF is the Java client's own tape of a session a person played on it, the
 * script its raw keyboard and mouse events (java RawRec). Each row the live
 * loop plays from those events is held against REF's row of the same tick:
 * the screens' ops first (the clicks GuiContainer made), then the rest of
 * the act the client's input made (the look, the key bindings held and
 * pressed, the hotbar, the focus, the left-click counter, control), then
 * the state (check_row). The first difference ends the run. */
static struct tape judge_tp;
static const char *judge_ref;
static long judge_rows, judge_gui_ticks, judge_clicks;

static int act_diff(const struct act *want, const struct act *got, char *buf, size_t n)
{
    if (want->has_look && (!got->has_look || memcmp(want->look, got->look, sizeof want->look)))
    {
        snprintf(buf, n, "look want [%.9g,%.9g,%.9g,%.9g] got [%.9g,%.9g,%.9g,%.9g]", (double)want->look[0],
                 (double)want->look[1], (double)want->look[2], (double)want->look[3], (double)got->look[0],
                 (double)got->look[1], (double)got->look[2], (double)got->look[3]);
        return 1;
    }
    if (want->has_in != got->has_in)
    {
        snprintf(buf, n, "in: Java's input block %s, this client's %s", want->has_in ? "ran" : "did not run",
                 got->has_in ? "ran" : "did not");
        return 1;
    }
    if (!want->has_in) return 0;
    for (int k = 0; k < K_N; ++k)
        if (want->keys.held[k] != got->keys.held[k] || want->keys.presses[k] != got->keys.presses[k])
        {
            snprintf(buf, n, "in.keys.%s want [%d,%d] got [%d,%d]", key_desc(k), want->keys.held[k],
                     want->keys.presses[k], got->keys.held[k], got->keys.presses[k]);
            return 1;
        }
    static const char *names[4] = {"hb", "focus", "lcc", "ctrl"};
    int wv[4] = {want->hb, want->focus, want->lcc, want->ctrl}, gv[4] = {got->hb, got->focus, got->lcc, got->ctrl};
    for (int i = 0; i < 4; ++i)
        if (wv[i] != gv[i])
        {
            snprintf(buf, n, "in.%s want %d got %d", names[i], wv[i], gv[i]);
            return 1;
        }
    return 0;
}

static int judge_row(int64_t t, const struct act *a)
{
    const struct jval *row = NULL;
    int rc;
    int64_t rt = -1;
    while ((rc = tape_next(&judge_tp, &row)) == 1)
        if (json_int(json_get(row, "t"), &rt) && rt >= S.tick) break;
    if (rc != 1)
    {
        printf("FAIL rawjudge rows t=%lld: Java's tape has no row %lld\n", (long long)t, (long long)t);
        return 1;
    }
    if (rt != t)
    {
        printf("FAIL rawjudge rows t=%lld: Java's next row is %lld\n", (long long)t, (long long)rt);
        return 1;
    }
    struct act want;
    char why[256];
    memset(&want, 0, sizeof want);
    if (session_parse_act(row, &want, why, sizeof why))
    {
        printf("FAIL rawjudge rows t=%lld: Java's act: %s\n", (long long)t, why);
        return 1;
    }
    static char w[16384], g[16384];
    ops_text(&want, w, sizeof w);
    ops_text(a, g, sizeof g);
    if (strcmp(w, g))
    {
        printf("FAIL rawjudge gui t=%lld: Java's screen sent %s, this client's %s\n", (long long)t,
               w[0] ? w : "nothing", g[0] ? g : "nothing");
        return 1;
    }
    judge_gui_ticks += w[0] != 0;
    judge_clicks += want.clicks;
    if (act_diff(&want, a, why, sizeof why))
    {
        printf("FAIL rawjudge rows t=%lld act.%s\n", (long long)t, why);
        return 1;
    }
    if (check_row("rawjudge", row, t)) return 1;
    ++judge_rows;
    return 0;
}

/* --mem: the session's bytes by owner (envmem.h) with the renderer's
 * records (the bands they share with the server are the server's) and
 * section meshes; the first row's and the largest */
static struct envmem mem_first, mem_peak, mem_row;
static int64_t mem_peak_row = -1;

static void play_mem(int64_t t)
{
    struct envmem *m = &mem_row;
    envmem_session(&SS, m);
    envmem_add(m, EM_RENDERER, "record window", (size_t)RW.rows * RW.rows * sizeof *RW.chunks);
    for (int i = 0; i < RW.rows * RW.rows; ++i) envmem_chunk(m, EM_RENDERER, "chunk records", RW.chunks[i]);
    envmem_add(m, EM_RENDERER, "section meshes", raster_live_mesh_bytes(RL));
    if (mem_peak_row < 0) mem_first = *m;
    if (mem_peak_row < 0 || envmem_total(m) > envmem_total(&mem_peak)) { mem_peak = *m; mem_peak_row = t; }
}

#ifdef PLAY_VIEW
__attribute__((unused)) static int play_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    const char *dir = "out/java/snapshots/fresh-play-s1";
    const char *assets = "out/java/render/forest-fast";
    hud_assets = "out/java/render/hud_mixed";
    mob_assets = assets;
    const char *out = NULL, *shot = NULL, *bench = NULL, *shot_tape = NULL;
    const char *shot_golden = NULL, *shot_budget = NULL;
    float shot_pt = 1.0F;   /* --shot-pt: the partial tick the shot is drawn at */
    const char *shot_items = NULL;
    int items_rc = 0, shot_screen_drawn = 0;
    const char *shots_file = NULL, *shot_golden_dir = NULL, *shot_dir = NULL;
    struct judge_shot *shots = NULL, **tick_shots = NULL;
    int nshots = 0, shots_rc = 0;
    const char *prefix_tape = NULL, *input_script = NULL;
    int prefix_until = -1;
    int threads = 0, idle = 0, aim = 0, until = INT_MAX;
    const char *replay = NULL, *check = NULL, *gui_judge = NULL, *gui_raw = NULL;
    int no_draw = 0;   /* --no-draw: the live loop draws nothing (its frames still turn the camera and take the hover) */
    int mem_on = 0;   /* --mem with --bench: the environment's bytes by owner, the renderer's included */
    int shot_tick = -1;
    int shot_near = 0;
    int shot_near_kind = -1;
#ifdef NETHERITE_DEV
    const char *dev_ops = NULL;
#endif
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) mob_assets = assets = argv[++i];
        else if (!strcmp(argv[i], "--hud-assets") && i + 1 < argc) hud_assets = argv[++i];
        else if (!strcmp(argv[i], "--tape") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!strcmp(argv[i], "--shot-tick") && i + 1 < argc) shot_tick = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-tape") && i + 1 < argc) shot_tape = argv[++i];
        else if (!strcmp(argv[i], "--shot-golden") && i + 1 < argc) shot_golden = argv[++i];
        else if (!strcmp(argv[i], "--shot-pt") && i + 1 < argc) shot_pt = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--shot-items") && i + 1 < argc) shot_items = argv[++i];
        else if (!strcmp(argv[i], "--shot-screen-drawn")) shot_screen_drawn = 1;
        else if (!strcmp(argv[i], "--shot-budget") && i + 1 < argc) shot_budget = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots_file = argv[++i];
        else if (!strcmp(argv[i], "--cw-hash") && i + 1 < argc)
        {
            /* the client world's light per section at these ticks, as the
             * oracle's --cw-diff prints it (CWHASH lines) */
            for (char *tok = strtok(argv[++i], ","); tok && ncwhash < 64; tok = strtok(NULL, ",")) cwhash_ticks[ncwhash++] = atoll(tok);
        }
        else if (!strcmp(argv[i], "--shot-golden-dir") && i + 1 < argc) shot_golden_dir = argv[++i];
        else if (!strcmp(argv[i], "--shot-dir") && i + 1 < argc) shot_dir = argv[++i];
        else if (!strcmp(argv[i], "--prefix-tape") && i + 1 < argc) prefix_tape = argv[++i];
        else if (!strcmp(argv[i], "--prefix-until") && i + 1 < argc) prefix_until = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input-script") && i + 1 < argc) input_script = argv[++i];
        else if (!strcmp(argv[i], "--until") && i + 1 < argc) until = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) idle = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--aim")) aim = 1;
        else if (!strcmp(argv[i], "--shot-near-mob")) shot_near = 1;
        else if (!strcmp(argv[i], "--shot-model") && i + 1 < argc) {
            const char *name = argv[++i];
            stage_kind = !strcmp(name, "dragon") ? 0 : !strcmp(name, "ender_crystal") ? 1 : -1;
            if (stage_kind < 0) { fprintf(stderr, "play: unknown shot model %s\n", name); return 2; }
        }
        else if (!strcmp(argv[i], "--shot-near-kind") && i + 1 < argc) {
            const char *name = argv[++i];
            shot_near = 1;
            shot_near_kind = !strcmp(name, "skeleton") ? HK_SKELETON :
                !strcmp(name, "creeper") ? HK_CREEPER :
                !strcmp(name, "spider") ? HK_SPIDER :
                !strcmp(name, "cave_spider") ? HK_CAVE_SPIDER :
                !strcmp(name, "enderman") ? HK_ENDERMAN :
                !strcmp(name, "witch") ? HK_WITCH :
                !strcmp(name, "slime") ? SK_SLIME :
                !strcmp(name, "silverfish") ? HK_SILVERFISH :
                !strcmp(name, "pigman") ? HK_PIGMAN :
                !strcmp(name, "ghast") ? GK_GHAST :
                !strcmp(name, "blaze") ? HK_BLAZE :
                !strcmp(name, "magma_cube") ? SK_MAGMA_CUBE :
                !strcmp(name, "villager") ? VK_VILLAGER :
                !strcmp(name, "iron_golem") ? VK_IRON_GOLEM :
                !strcmp(name, "squid") ? AK_SQUID :
                !strcmp(name, "bat") ? AK_BAT :
                !strcmp(name, "mooshroom") ? AK_MOOSHROOM : -1;
            if (shot_near_kind < 0) { fprintf(stderr, "play: unknown shot kind %s\n", name); return 2; }
        }
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench = argv[++i];
        else if (!strcmp(argv[i], "--mem")) mem_on = 1;
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else if (!strcmp(argv[i], "--gui-judge") && i + 1 < argc) gui_judge = argv[++i];
        else if (!strcmp(argv[i], "--gui-raw") && i + 1 < argc) gui_raw = argv[++i];
        else if (!strcmp(argv[i], "--judge") && i + 1 < argc) judge_ref = argv[++i];
        else if (!strcmp(argv[i], "--no-draw")) no_draw = 1;
        else if (!strcmp(argv[i], "--mac-keys")) mac_keys = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 16 || H < 16 || W > 4096 || H > 4096)
            { fprintf(stderr, "play: --size takes WxH\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--hide-gui")) play_hide_gui = 1;
        else if (!strcmp(argv[i], "--obs"))
        {
            W = RASTER_OBS_W; H = RASTER_OBS_H;
            play_hide_gui = RASTER_OBS_HIDE_GUI;
            play_view_rd = RASTER_OBS_RD;
        }
        else if (!strcmp(argv[i], "--obs-dump") && i + 1 < argc) obs_dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--obs-mesh") && i + 1 < argc) play_dmesh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mesh-check")) play_mesh_check = 1;
        else if (!strcmp(argv[i], "--render-prec") && i + 1 < argc)
        {
            play_prec = rp_parse(argv[++i]);
            if (play_prec < 0) { fprintf(stderr, "play: --render-prec takes exact, fast or fast:STAGE,... (xform edge light color fog shade)\n"); return 2; }
            raster_entity_set_prec(play_prec);
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
        {
            const char *m = argv[++i];
            frames_mode = !strcmp(m, "auto") ? 0 : !strcmp(m, "c") ? 1 : !strcmp(m, "device") ? 2 : -1;
            if (frames_mode < 0) { fprintf(stderr, "play: --frames takes auto, c or device\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--frames-check") && i + 1 < argc)
        {
            frames_check = atoi(argv[++i]);
            if (frames_check < 1) { fprintf(stderr, "play: --frames-check takes N >= 1\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--render-distance") && i + 1 < argc) play_view_rd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--options") && i + 1 < argc) { if (!load_options(argv[++i])) return 2; }
#ifdef NETHERITE_DEV
        else if (!strcmp(argv[i], "--dev-ops") && i + 1 < argc) dev_ops = argv[++i];
#endif
        else if (argv[i][0] != '-') dir = argv[i];
        else { fprintf(stderr, "usage: play [SNAPSHOT_DIR] [--assets SCENE] [--tape OUT] [--threads N] [--size WxH] [--hide-gui] [--render-distance N] [--obs]\n"); return 2; }
    }
    if (threads <= 0) threads = SDL_GetNumLogicalCPUCores();
    fancy_assets = recorded_fancy(assets);
    if (shots_file)
    {
        /* the judge's lines whole between the stderr lines in one log */
        setvbuf(stdout, NULL, _IOLBF, 0);
        FILE *f = fopen(shots_file, "r");
        if (!f || !replay || (!shot_golden_dir && !obs_dump_dir)) { fprintf(stderr, "play: --shots needs a readable file, --replay and --shot-golden-dir or --obs-dump\n"); return 2; }
        char line[512];
        while (fgets(line, sizeof line, f))
        {
            struct judge_shot js;
            memset(&js, 0, sizeof js);
            char b[4][64], fl[16] = "";
            int k = sscanf(line, "%d %f %63s %63s %63s %63s %15s", &js.tick, &js.pt, b[0], b[1], b[2], b[3], fl);
            if (k < 2) continue;
            if (k >= 6 && strcmp(b[0], "-")) snprintf(js.budget, sizeof js.budget, "%s %s %s %s", b[0], b[1], b[2], b[3]);
            js.drawn = k >= 7 && !strcmp(fl, "drawn");
            shots = realloc(shots, (size_t)(nshots + 1) * sizeof *shots);
            shots[nshots++] = js;
            if (js.tick > shot_tick) shot_tick = js.tick;
        }
        fclose(f);
        if (!nshots) { fprintf(stderr, "play: %s has no shots\n", shots_file); return 2; }
        tick_shots = malloc((size_t)nshots * sizeof *tick_shots);
        shot = "-";
    }

    char outbuf[512];
    if (!out && !check && !gui_judge)   /* a check writes no tape */
    {
        time_t now = time(NULL);
        struct tm tm;
        gmtime_r(&now, &tm);
        char stamp[32];
        strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &tm);
        /* out/native/play is this binary; the tapes live beside it */
        if (mkdir("out/native/tapes", 0755) != 0 && errno != EEXIST)
        {
            fprintf(stderr, "play: cannot create out/native/tapes (run from the repo root)\n");
            return 1;
        }
        snprintf(outbuf, sizeof outbuf, "out/native/tapes/%s.jsonl", stamp);
        out = outbuf;
    }

    Uint64 t0 = SDL_GetTicksNS();
    if (!play_open(dir, check != NULL)) return 1;
#ifdef NETHERITE_DEV
    if (!load_sidecar(dev_ops)) return 2;
    SS.extra = (const struct jval *const *)sidecar;
    SS.nextra = nsidecar;
#endif
    if (check) return check_rows(check, dir);
    /* the join's hint toast: a seed start's client took the join's S37 at
     * clock 50 (Oracle.tick 1), whatever tick its snapshot was taken at, as
     * the oracle's frames show it (a frame after tick T reads the clock
     * (T + 1) * 50: off screen until tick 51, in place from 55); a checkpoint's long
     * before, so it is fully in. A snapshot without stats.json has an empty
     * mirror: only a seed start's player is known to hold no achievement (a
     * checkpoint's join S37 carries the save's) */
    {
        const char *kind = json_str(json_get(json_get(SS.hdr, "start"), "kind"));
        int seed_start = kind && !strcmp(kind, "seed");
        if (S.stats_json || seed_start)
            surv_client_join_hint(&CP, seed_start || S.tick <= 2 ? 50 : -60000);
        else if (kind && !strcmp(kind, "checkpoint")) legacy_client_stats();
    }
    hud_options(hud_assets);
    /* ScaledResolution at this frame's size: the options' guiScale (0 is
     * auto) caps the largest scale the size allows (2 at 854x480, 1 below
     * 640x480) */
    {
        int64_t g = -1;
        json_int(json_get(json_get(SS.hdr, "options"), "gui"), &g);
        int cap = g < 0 ? hud_scale : g == 0 ? 1000 : (int)g;
        hud_scale = gui_screen_layout(NULL, W, H, cap).scale;
    }
    gui_input_reset(&gin);
    gin.width = W;
    gin.height = H;
    gin.gui_scale = hud_scale;
    if (gui_judge)
    {
        if (!gui_raw) { fprintf(stderr, "play: --gui-judge needs --gui-raw\n"); return 2; }
        return judge_gui(gui_judge, gui_raw);
    }
    /* dead livings are freed between rows in every mode that plays on
     * (grave.h), as check_rows does: without it each dead or unloaded
     * living keeps its pool slot, and a swamp's slimes at full moon
     * (moon-swamp-s2, swamp-night-s2) take all 65,536 in about 540 ticks */
    grave_enable(GRAVE_THRESHOLD);
    CP.update_renderer = live_update_renderer;
    if (!SS.server_rows)
    {
        fprintf(stderr, "play: %s is not a whole-server snapshot (onlyPlayers is not 1)\n", dir);
        return 1;
    }
    view_dim = SP.dimension;
    view_assets = assets;
    view_threads = threads;
    if (!tape_begin(dir, out) || !rw_build()) return 1;
    if (start_at_join) CP.left_click_counter = 10000;
    /* GuiIngame.updateCounter: the frame after row t shows t + 1, as the
     * oracle's do from any start */
    hud_update_counter = (int)S.tick;
    RL = raster_live_new(assets, &RW, W, H, threads);
    if (play_dmesh) raster_live_device_mesh(RL, play_dmesh);
    raster_live_set_prec(RL, play_prec);
    raster_live_mesh_check(RL, play_mesh_check);
    raster_rec_verify(play_mesh_check);
    play_anim_open(assets);
    surv_fx.mesher = raster_live_mesher(RL);
    light_table();
    hand_assets = assets;
    CP.arm_pitch = CP.prev_arm_pitch = CP.rotation_pitch;
    CP.arm_yaw = CP.prev_arm_yaw = CP.rotation_yaw;
    yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
    pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
    /* fogColor1 and rendererUpdateCount: renderer_tick's first tick starts
     * them as if the rows before it had run */
    fc1 = fc2 = 0.0F;
    /* ItemRenderer.updateEquippedItem since the join: a stack held then is
     * itemToRender, taken while the progress stood at 0 */
    {
        const struct surv_stack *held = &CP.sv.inv[CP.hotbar];
        if (held->count > 0)
        {
            equip.present = 1;
            equip.item = held->item;
            equip.damage = held->damage;
            equip.gen = held->gen;
            equip.slot = CP.hotbar;
        }
    }
    fprintf(stderr, "play: %s seed %lld tick %lld, %d chunks, render world %dx%d, %d threads, loaded in %.2fs\n",
            dir, (long long)S.seed, (long long)S.tick, S.nchunks, RW.rows, RW.rows, threads,
            (double)(SDL_GetTicksNS() - t0) / 1e9);
    fprintf(stderr, "play: tape %s\n", out);

    if (bench)
    {
        /* a played tape's inputs through the same ticks, one frame after each
         * tick at pt 1, every stage timed */
        struct tape tp;
        if (!tape_open(&tp, bench)) { fprintf(stderr, "play: cannot read %s\n", bench); return 1; }
        enum { NS = 9 };
        static const char *names[NS] = {"tick", "mesh", "lists", "sky", "record", "raster", "hud", "rs", "frame"};
        double *ms[NS];
        int cap = 1 << 16, n = 0;
        for (int k = 0; k < NS; ++k) ms[k] = calloc((size_t)cap, sizeof(double));
        long tris = 0;
        int draws = 0;
        const struct jval *row;
        while (tape_next(&tp, &row) == 1 && n < cap)
        {
            int64_t t;
            if (!json_int(json_get(row, "t"), &t) || t < S.tick) continue;
            struct act a;
            parse_act(row, &a);
            Uint64 a0 = SDL_GetTicksNS();
            play_tick(t, &a);
            view_follow();
            renderer_tick();
            rw_after_tick();
            yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
            pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
            double tick_ms = (double)(SDL_GetTicksNS() - a0) / 1e6;
            if (t % 20 == 0 || tick_ms > 50)
                fprintf(stderr, "  t=%lld writes %d chunks %d changed-sections %d column-resets %d  tick %.1f ms\n",
                        (long long)t, rw_writes, rw_chunks, rw_diff_secs, rw_column_chunks, tick_ms);
            struct rs_in in;
            struct rs_out o;
            Uint64 r0 = SDL_GetTicksNS();
            build_rs(&in, 1.0F);
            rs_compute(&in, &o);
            double rs_ms = (double)(SDL_GetTicksNS() - r0) / 1e6;
            struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day, CP.e.pos_y, play_clouds};
            Uint64 f0 = SDL_GetTicksNS();
            unsigned char *img = malloc((size_t)W * H * 3);
            raster_live_frame(RL, &li, img);
            raster_live_translucent(RL, img);
            live_worldfx(&in, &o, 1.0F, img, 0);
            Uint64 h0 = SDL_GetTicksNS();
            live_hud(img, t);
            double hud_ms = (double)(SDL_GetTicksNS() - h0) / 1e6;
            double frame_ms = (double)(SDL_GetTicksNS() - f0) / 1e6;
            free(img);
            if (mem_on) play_mem(t);
            const struct raster_live_stats *st = raster_live_stats(RL);
            double v[NS] = {tick_ms, st->mesh_ms, st->lists_ms, st->sky_ms, st->record_ms, st->raster_ms, hud_ms, rs_ms, frame_ms};
            for (int k = 0; k < NS; ++k) ms[k][n] = v[k];
            tris += st->triangles;
            draws += st->draws;
            ++n;
        }
        tape_close(&tp);
        if (mem_on)
        {
            envmem_print(stderr, "mem start", &mem_first, 1);
            char label[64];
            snprintf(label, sizeof label, "mem peak (row %lld)", (long long)mem_peak_row);
            envmem_print(stderr, label, &mem_peak, 1);
        }
        fprintf(stderr, "play: bench %s: %d ticks, one frame each, %d threads; %ld triangles and %d draws per frame on average\n",
                bench, n, threads, n ? tris / n : 0, n ? draws / n : 0);
        fprintf(stderr, "  stage      mean     p50     p95     max   (ms; frame excludes tick)\n");
        for (int k = 0; k < NS; ++k)
        {
            double *x = ms[k], sum = 0;
            for (int i = 0; i < n; ++i) sum += x[i];
            /* insertion sort is fine at a few thousand */
            for (int i = 1; i < n; ++i) { double y = x[i]; int j = i - 1; while (j >= 0 && x[j] > y) { x[j + 1] = x[j]; --j; } x[j + 1] = y; }
            fprintf(stderr, "  %-7s %7.2f %7.2f %7.2f %7.2f\n", names[k], n ? sum / n : 0,
                    n ? x[n / 2] : 0, n ? x[(int)(n * 0.95)] : 0, n ? x[n - 1] : 0);
        }
        fclose(tape_out);
        return 0;
    }

    if (shot)
    {
        if (shot_tape)
        {
            struct tape tp;
            if (!tape_open(&tp, shot_tape)) { fprintf(stderr, "play: cannot read %s\n", shot_tape); return 1; }
            const struct jval *row;
            while (tape_next(&tp, &row))
            {
                int64_t t;
                if (!json_int(json_get(row, "t"), &t) || t < S.tick) continue;
                if (t > until) break;
                struct act a;
                parse_act(row, &a);
                tape_row(t, &a);
                play_tick(t, &a);
                view_follow();
                renderer_tick();
                rw_after_tick();
                yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
                pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
            }
            tape_close(&tp);
            fprintf(stderr, "play: shot replay through t=%d, hotbar %d, held %d:%d x%d\n",
                    until, CP.hotbar, CP.sv.inv[CP.hotbar].item,
                    CP.sv.inv[CP.hotbar].damage, CP.sv.inv[CP.hotbar].count);
        }
        /* headless: a frame to fill the cache, IDLE ticks with no input held,
         * frames at pt 1, then the cache checked against a full re-mesh */
        struct key_state none;
        memset(&none, 0, sizeof none);
        unsigned char *img = malloc((size_t)W * H * 3), *fresh = malloc((size_t)W * H * 3);
        {
            struct rs_in in;
            struct rs_out o;
            build_rs(&in, 1.0F);
            rs_compute(&in, &o);
            struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day, CP.e.pos_y, play_clouds};
            raster_live_frame(RL, &li, img);
            raster_live_translucent(RL, img);
            live_worldfx(&in, &o, 1.0F, img, 0);
        }
        Uint64 a0 = SDL_GetTicksNS();
        int64_t t = S.tick;
        FILE *raw_f = NULL;
        struct jval *raw_ev = NULL;
        if (replay && gui_raw)
        {
            raw_f = fopen(gui_raw, "r");
            if (!raw_f) { fprintf(stderr, "play: cannot read %s\n", gui_raw); return 1; }
            raw_ev = script_next(raw_f);
        }
        if (replay)
        {
            struct tape tp;
            if (!tape_open(&tp, replay)) { fprintf(stderr, "play: cannot read %s\n", replay); return 1; }
            /* the device renderer opens while the replay runs, for a
             * run of shots long enough to repay its context (most of a
             * second to create on the shared device) */
            if (nshots >= FDEV_MIN_SHOTS && !obs_dump_dir) fdev_start();
            const struct jval *row;
            while (tape_next(&tp, &row) == 1)
            {
                int64_t tick;
                if (!json_int(json_get(row, "t"), &tick) || tick < S.tick) continue;
                if (shot_tick >= 0 && tick > shot_tick) break;
                struct act a;
                parse_act(row, &a);
                if (raw_f)
                {
                    /* the screens' ops made again from the raw events, as
                     * the GUI judge makes them: the drag the frame shows */
                    a.clicks = a.trsels = a.gui_close = a.closes = 0;
                    a.gui_in = &gin;
                    feed_raw(raw_f, &raw_ev, tick);
                }
                play_tick(tick, &a);
                shot_note_tick((int)tick);
                tape_row(tick, &a);
                if (raw_f) { gui_input_drawn(&gin, &CP); gui_input_frame(&gin); }
                view_follow();
                renderer_tick();
                rw_after_tick();
                yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
                pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
                t = tick;
                ++idle;
                /* the shots after this tick: on the device together (one
                 * render), else one by one in C */
                int nt = 0;
                for (int k = 0; k < nshots; ++k)
                    if (shots[k].tick == tick) tick_shots[nt++] = &shots[k];
                int dev = nt && !obs_dump_dir && (nshots >= FDEV_MIN_SHOTS || frames_mode == 2) ? fdev_ready() : 0;
                if (dev < 0)
                {
                    printf("FAIL frame judge t=%d: --frames device and no device renderer\n", (int)tick);
                    if (!shots_rc) shots_rc = 1;
                }
                else if (dev)
                {
                    int r = judge_tick_device(tick_shots, nt, shot_golden_dir, shot_dir, shot_items, aim, assets);
                    if (r && !shots_rc) shots_rc = r;
                }
                else
                    for (int k = 0; k < nt; ++k)
                    {
                        int r = judge_shot_draw(tick_shots[k], shot_golden_dir, shot_dir, shot_items, aim, assets);
                        if (r && !shots_rc) shots_rc = r;
                        shot_fx_interp(tick_shots[k]->pt);
                    }
                if (shot_tick >= 0 && tick == shot_tick) break;
            }
            tape_close(&tp);
            if (raw_f) fclose(raw_f);
            if (raw_ev) json_free(raw_ev);
            if (nshots)
            {
                fclose(tape_out);
                free(shots);
                fprintf(stderr, "play: %d shots over %d ticks\n", nshots, idle);
                if (fdev_shots)
                    fprintf(stderr, "play: %d world frames on the device, %.1f ms of device time; %d checked against the C "
                            "renderer, %d different\n", fdev_shots, fdev_ms, fdev_checked, fdev_bad);
                fdev_finish();
                free(tick_shots);
                return shots_rc;
            }
        }
        for (int i = 0; !replay && i < idle; ++i, ++t)
        {
            struct act a;
            memset(&a, 0, sizeof a);
            a.has_look = 1;
            a.look[0] = yaw; a.look[1] = pitch; a.look[2] = pyaw; a.look[3] = ppitch;
            a.has_in = 1;
            a.keys = none;
            a.hb = CP.hotbar;
            a.focus = 1;
            a.lcc = CP.screen_inventory ? 9999 : CP.left_click_counter > 0 ? CP.left_click_counter - 1 : 0;
            tape_row(t, &a);
            play_tick(t, &a);
            view_follow();
            renderer_tick();
            rw_after_tick();
            yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
            pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
        }
        double tick_ms = idle ? (double)(SDL_GetTicksNS() - a0) / 1e6 / idle : 0;
        if (aim)
        {
            /* turn to the nearest mob, to see its box */
            double best = 1e30, tx = 0, ty = 0, tz = 0;
            for (int i = 0; i < SR.d->anw.n; ++i)
            {
                const struct an_ent *a = an_ent_at(SR.d->anw.slot[i]);
                if (!a || !a->used || !a->is_living || !a->livh || lv_get(a->livh) == lv_get(SR.player_livh)) continue;
                const struct entity *e = &lv_get(a->livh)->e;
                double dx = e->pos_x - CP.e.pos_x, dy = e->pos_y - CP.e.pos_y, dz = e->pos_z - CP.e.pos_z;
                if (fabs(dy) > 4) continue;   /* on the surface near the eye */
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) { best = d; tx = dx; ty = (e->bounding_box.min_y + e->bounding_box.max_y) / 2 - CP.e.pos_y; tz = dz; }
            }
            if (best < 1e30)
            {
                yaw = pyaw = (float)(-atan2(tx, tz) * 180.0 / M_PI);
                pitch = ppitch = (float)(-atan2(ty, sqrt(tx * tx + tz * tz)) * 180.0 / M_PI);
                fprintf(stderr, "play: aimed at a mob %.1f blocks away (yaw %.1f pitch %.1f)\n", sqrt(best), yaw, pitch);
            }
        }
        if (shot_near)
        {
            const struct living *chosen = NULL;
            double best = -1e30, cx = 0, cz = 0;
            static const int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},
                                           {1,1},{1,-1},{-1,1},{-1,-1}};
            for (int i = 0; i < SR.d->anw.n; ++i) {
                const struct an_ent *a = an_ent_at(SR.d->anw.slot[i]);
                if (!a || !a->used || !a->is_living || !a->livh) continue;
                int k = lv_get(a->livh)->kind;
                if (shot_near_kind >= 0 ? k != shot_near_kind :
                    (k != AK_PIG && k != AK_COW && k != AK_SHEEP && k != AK_CHICKEN)) continue;
                const struct entity *e = &lv_get(a->livh)->e;
                double px = e->pos_x - CP.e.pos_x, pz = e->pos_z - CP.e.pos_z;
                double dist = sqrt(px * px + pz * pz);
                for (int j = 0; j < 8; ++j) {
                    double len = hypot((double)dirs[j][0], (double)dirs[j][1]);
                    double dx = dirs[j][0] / len, dz = dirs[j][1] / len;
                    double x = e->pos_x + dx * 5, z = e->pos_z + dz * 5;
                    double score = -dist * 0.02;
                    for (int step = 1; step <= 5; ++step) {
                        int bx = (int)floor(e->pos_x + dx * step);
                        int bz = (int)floor(e->pos_z + dz * step);
                        int by = (int)floor(e->pos_y);
                        for (int h = 0; h <= 2; ++h)
                            score += world_get_block(&SR.pop.world, bx, by + h, bz) == 0 ? 2 : -6;
                    }
                    if (score > best) { best = score; chosen = lv_get(a->livh); cx = x; cz = z; }
                }
            }
            if (chosen) {
                /* The tick and its tape are finished. Move only the camera for this shot. */
                CP.e.prev_pos_x = CP.e.pos_x = cx;
                CP.e.prev_pos_y = CP.e.pos_y = chosen->e.pos_y + 2.0;
                CP.e.prev_pos_z = CP.e.pos_z = cz;
                yaw = pyaw = (float)(-atan2(chosen->e.pos_x - cx,
                    chosen->e.pos_z - cz) * 180.0 / M_PI);
                pitch = ppitch = 24.0f;
                fprintf(stderr, "play: shot camera 5 blocks from a kind %d at %.1f %.1f %.1f, clear score %.1f\n",
                        chosen->kind, chosen->e.pos_x, chosen->e.pos_y, chosen->e.pos_z, best);
            }
        }
        double ms[3];
        int meshed[3];
        for (int k = 0; k < 3; ++k)
        {
            struct rs_in in;
            struct rs_out o;
            build_rs(&in, shot_pt);
            rs_compute(&in, &o);
            struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day, CP.e.pos_y, play_clouds};
            Uint64 f0 = SDL_GetTicksNS();
            meshed[k] = raster_live_frame(RL, &li, img);
            ms[k] = (double)(SDL_GetTicksNS() - f0) / 1e6;
        }
        {
            /* the cache against a full re-mesh, both passes; IMG keeps the
             * opaque frame the world's passes draw over */
            unsigned char *cached = malloc((size_t)W * H * 3);
            memcpy(cached, img, (size_t)W * H * 3);
            raster_live_translucent(RL, cached);
            struct rs_in in;
            struct rs_out o;
            build_rs(&in, shot_pt);
            rs_compute(&in, &o);
            struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day, CP.e.pos_y, play_clouds};
            raster_live_stale_all(RL);
            raster_live_frame(RL, &li, fresh);
            raster_live_translucent(RL, fresh);
            long differ = 0;
            for (size_t k = 0; k < (size_t)W * H * 3; ++k) differ += cached[k] != fresh[k];
            free(cached);
            fprintf(stderr, "play: cache check after %d ticks: %s (%ld bytes differ from a full re-mesh)\n",
                    idle, differ ? "STALE" : "exact", differ);
        }
        /* the world's passes over the frame, then (the frame judge) the
         * same passes without the drops over a copy of the world frame */
        unsigned char *world0 = NULL, *bare = NULL;
        float *depth0 = NULL;
        if (shot_items)
        {
            world0 = malloc((size_t)W * H * 3);
            bare = malloc((size_t)W * H * 3);
            depth0 = malloc((size_t)W * H * sizeof *depth0);
            memcpy(world0, img, (size_t)W * H * 3);
            memcpy(depth0, raster_live_depth_mut(RL), (size_t)W * H * sizeof *depth0);
        }
        frame_now_ms = ((int64_t)shot_tick + 1) * 50;
        shot_world(img, shot_pt, aim, assets, 1, NULL);
        if (shot_items)
        {
            memcpy(bare, world0, (size_t)W * H * 3);
            memcpy(raster_live_depth_mut(RL), depth0, (size_t)W * H * sizeof *depth0);
            shot_skip_drops = 1;
            /* what hides a drop: the bare frame's depth (the terrain, the
             * mobs, the tile entities) */
            shot_world(bare, shot_pt, aim, assets, 0, depth0);
            shot_skip_drops = 0;
            if (shot_presence(img, bare, depth0, shot_items, shot_tick, shot_pt)) items_rc = 3;
            free(world0); free(bare); free(depth0);
        }
        live_hud(img, t);
        /* the pointer where the oracle's agent frames have it: the window's
         * centre, where the screen's ungrab warps it; --shot-screen-drawn: an
         * earlier frame drew this screen there, so GuiInventory's stored
         * pointer (the preview's gaze) is the centre too, not the fresh
         * screen's 0, 0 */
        {
            struct gui_screen_layout gl = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
            pointer_note_shot(t);
            pointer_shot_tick = t;
            shot_inv_drawn(shot_screen_drawn);
            if (shot_screen_drawn && CP.screen_inventory)
            {
                inv_was_open = 1;
                inv_mouse_x = (float)centre_mouse_x(&gl);
                inv_mouse_y = (float)centre_mouse_y(&gl);
            }
            live_screens(img, 0, centre_mouse_x(&gl), centre_mouse_y(&gl));
        }
        live_toast(img, t + 1);
        int rc = raster_png(shot, W, H, img);
        if (!rc && shot_golden && shot_compare(img, shot_golden, shot_budget, shot_tick, shot_pt)) rc = 3;
        if (!rc) rc = items_rc;
        fprintf(stderr, "play: %d ticks at %.2f ms each; frames %.1f ms (%d meshed), %.1f ms, %.1f ms; pos %.3f %.3f %.3f; %s\n",
                idle, tick_ms, ms[0], meshed[0], ms[1], ms[2], CP.e.pos_x, CP.e.pos_y, CP.e.pos_z, shot);
        fclose(tape_out);
        return rc;
    }

    if (replay)
    {
        /* headless: another session's inputs (each row's t and act, nothing
         * else is read) through the live loop's tick path, this client's own
         * tape written as it goes */
        struct tape tp;
        if (!tape_open(&tp, replay)) { fprintf(stderr, "play: cannot read %s\n", replay); return 1; }
        const struct jval *row;
        int64_t t = S.tick;
        int rc;
        while ((rc = tape_next(&tp, &row)) == 1)
        {
            int64_t rt;
            if (!json_int(json_get(row, "t"), &rt) || rt < S.tick) continue;
            if (rt > until) break;
            if (rt != t) { fprintf(stderr, "play: input row t=%lld where t=%lld is due\n", (long long)rt, (long long)t); return 1; }
            struct act a;
            parse_act(row, &a);
            tape_row(t, &a);
            play_tick(t, &a);
            view_follow();
            renderer_tick();
            rw_after_tick();
            ++t;
        }
        tape_close(&tp);
        fclose(tape_out);
        if (rc < 0) { fprintf(stderr, "play: %s: a row does not parse\n", replay); return 1; }
        fprintf(stderr, "play: %lld ticks played from %s, tape %s\n", (long long)(t - S.tick), replay, out);
        return 0;
    }

    int64_t start_tick = S.tick;
    if (prefix_tape)
    {
        struct tape tp;
        if (!tape_open(&tp, prefix_tape)) { fprintf(stderr, "play: cannot read %s\n", prefix_tape); return 1; }
        const struct jval *row;
        while (tape_next(&tp, &row) == 1)
        {
            int64_t rt;
            if (!json_int(json_get(row, "t"), &rt) || rt < S.tick) continue;
            if (rt >= prefix_until) break;
            struct act a;
            parse_act(row, &a);
            tape_row(rt, &a);
            play_tick(rt, &a);
            renderer_tick();
            rw_after_tick();
            yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
            pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
            start_tick = rt + 1;
        }
        tape_close(&tp);
        fprintf(stderr, "play: prefix replay stopped at t=%lld\n", (long long)start_tick);
    }

    FILE *script_file = NULL;
    struct jval *script_row = NULL;
    /* a frame-structured script (its first line {"framed":1}, csrc/play/
     * raw2script.jq): each {"frame"} line ends one recorded frame's events
     * and says how many ticks the frame ran and the camera motion it turned
     * the player by after them, so the loop runs the recorded client's
     * frames one for one instead of one tick per frame */
    int framed_script = 0;
    if (input_script)
    {
        script_file = fopen(input_script, "r");
        if (!script_file) { fprintf(stderr, "play: cannot read %s\n", input_script); return 1; }
        script_row = script_next(script_file);
        if (script_row && json_get(script_row, "framed"))
        {
            framed_script = 1;
            json_free(script_row);
            script_row = script_next(script_file);
        }
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    }
    if (judge_ref)
    {
        if (!framed_script) { fprintf(stderr, "play: --judge needs a frame-structured --input-script\n"); return 2; }
        if (!tape_open(&judge_tp, judge_ref)) { fprintf(stderr, "play: cannot read %s\n", judge_ref); return 1; }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "play: SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("netherite (C)", W * SCALE, H * SCALE, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H) : NULL;
    if (!tex) { fprintf(stderr, "play: SDL: %s\n", SDL_GetError()); return 1; }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderVSync(ren, 0);
    SDL_SetWindowRelativeMouseMode(win, !input_script);

    unsigned char *rgb = malloc((size_t)W * H * 3);
    struct live L;
    memset(&L, 0, sizeof L);
    L.win = win;
    L.grabbed = 1;
    L.running = 1;
    L.scripted = input_script != NULL;
    int respawn_landing = 0;
    int64_t t = start_tick;
    L.tick_at = (double)SDL_GetTicksNS() / 1e9;
    double fps_at = L.tick_at, frame_ms = 0;
    int frames = 0, meshed_total = 0, nents = 0, judge_fail = 0;
    float fps = 0;
    int64_t clock_last = -1, clock_last_t = 0;

    while (L.running)
    {
        int framed = 0, fr_ticks = 0, fr_rel = 0, nclock = 0, fr_ptr = 0, fr_px = 0, fr_py = 0;
        float fr_pt = 1.0F, fr_rx = 0, fr_ry = 0;
        int64_t clock_t[64], clock_ms[64];
        while (script_row && framed_script)
        {
            int64_t event_tick = -1;
            json_int(json_get(script_row, "t"), &event_tick);
            if (json_get(script_row, "quit")) L.running = 0;
            else if (json_get(script_row, "frame"))
            {
                if (event_tick != t)
                {
                    printf("FAIL rawjudge rows t=%lld: the recorded client's frame starts at tick %lld, this client is at tick %lld\n",
                           (long long)(t < event_tick ? t : event_tick), (long long)event_tick, (long long)t);
                    judge_fail = 1;
                    L.running = 0;
                    break;
                }
                int64_t v = 0;
                uint64_t bits;
                double d;
                json_int(json_get(script_row, "ticks"), &v);
                fr_ticks = (int)v;
                if (json_double(json_get(script_row, "pt"), &bits)) { memcpy(&d, &bits, 8); fr_pt = (float)d; }
                const struct jval *rel = json_get(script_row, "rel");
                if (rel && json_len(rel) >= 2 && json_double(json_at(rel, 0), &bits))
                {
                    memcpy(&d, &bits, 8);
                    fr_rx = (float)d;
                    json_double(json_at(rel, 1), &bits);
                    memcpy(&d, &bits, 8);
                    fr_ry = (float)d;
                    fr_rel = 1;
                }
                /* the pointer Java's frame drew the screen with (Mouse.getX
                 * and getY at its draw): what its hover reads, however it got
                 * there (a warp, a stand-in's pointer) */
                const struct jval *px = json_get(script_row, "px");
                if (px && json_len(px) >= 2 && json_int(json_at(px, 0), &v))
                {
                    fr_px = (int)v;
                    json_int(json_at(px, 1), &v);
                    fr_py = (int)v;
                    fr_ptr = 1;
                }
                framed = 1;
            }
            else if (json_get(script_row, "clock"))
            {
                int64_t ms = 0;
                json_int(json_get(script_row, "clock"), &ms);
                if (nclock < 64) { clock_t[nclock] = event_tick; clock_ms[nclock] = ms; ++nclock; }
            }
            else
            {
                if (event_tick != t)
                {
                    fprintf(stderr, "play: script event for t=%lld in the frame at t=%lld\n", (long long)event_tick,
                            (long long)t);
                    return 1;
                }
                SDL_Event e;
                if (script_event(script_row, &e)) SDL_PushEvent(&e);
            }
            json_free(script_row);
            script_row = script_next(script_file);
            if (framed || !L.running) break;
        }
        if (framed_script && !framed) L.running = 0;
        while (script_row && !framed_script)
        {
            int64_t event_tick = -1;
            json_int(json_get(script_row, "t"), &event_tick);
            if (event_tick > t) break;
            if (event_tick < t) { fprintf(stderr, "play: late script event at t=%lld\n", (long long)event_tick); return 1; }
            const struct jval *look = json_get(script_row, "look");
            SDL_Event e;
            if (json_get(script_row, "quit")) L.running = 0;
            else if (look && json_len(look) >= 2)
            {
                uint64_t bits;
                double d;
                if (json_double(json_at(look, 0), &bits)) { memcpy(&d, &bits, 8); yaw = pyaw = (float)d; }
                if (json_double(json_at(look, 1), &bits)) { memcpy(&d, &bits, 8); pitch = ppitch = (float)d; }
            }
            else if (script_event(script_row, &e)) SDL_PushEvent(&e);
            json_free(script_row);
            script_row = script_next(script_file);
        }
        if (!L.running) break;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) live_event(&L, &ev);
        if (!L.running) break;

        /* the ticks due: each one's input is the state the frames left */
        double now = input_script ? L.tick_at : (double)SDL_GetTicksNS() / 1e9;
        if (L.paused) L.tick_at = now + 0.05;
        if (now - L.tick_at > 1.0) L.tick_at = now - 0.05;   /* a stall drops ticks rather than racing */
        int left = fr_ticks;
        while (!L.paused && (framed ? left > 0 : now >= L.tick_at))
        {
            --left;
            struct act a;
            memset(&a, 0, sizeof a);
            a.has_look = 1;
            a.look[0] = yaw; a.look[1] = pitch; a.look[2] = pyaw; a.look[3] = ppitch;
            /* the tick after a respawn from the death screen: its S07 lands
             * with the tick's packets and closes the screen before the input
             * block, which runs (and the respawned player has the mouse) */
            int respawned = respawn_landing;
            respawn_landing = 0;
            /* an S2E the last server tick sent (the villager let go, the
             * chest out of reach) closes the container screen in this
             * tick's pump: the input block runs and the game takes the mouse
             * (displayGuiScreen(null)'s setIngameFocus) */
            if (CP.screen_inventory && CP.open_container != &CP.own_container && s2c_close_pending())
                respawned = 1;
            if (respawned && !L.grabbed)
            {
                L.grabbed = 1;
                SDL_SetWindowRelativeMouseMode(win, true);
            }
            /* the credits' own end, this tick's updateScreen clock */
            if (CP.screen_credits && credits_roll_ends(credits_ticks + 1)) L.respawn = 1;
            a.has_in = ((!CP.screen_inventory || cp_gui_container(&CP) == &CP.own_container) &&
                        !CP.screen_gameover && !CP.screen_credits) || L.close_screen || respawned ||
                       (L.respawn && (CP.screen_gameover || CP.screen_credits));
            a.keys = L.keys;
            /* the wheel turns currentItem in the mouse loop, before the
             * snapshot, from the slot the tick's packets left (a respawn's
             * fresh player holds slot 0): resolved inside the tick */
            a.hb = -1;
            a.hb_live = 1;
            a.hb_wheel = L.wheel;
            L.wheel = 0;
            a.o_tpv = a.o_hide = a.o_smooth = a.o_rd = a.o_dbg = -1;
            if (L.opts_changed)
            {
                a.has_opts = 1;
                a.o_tpv = L.opt_tpv;
                a.o_hide = L.opt_hide;
                a.o_smooth = L.opt_smooth;
                a.o_dbg = L.opt_dbg;
                a.o_rd = play_rd;
                a.rd = play_rd;   /* the row's rd the server's view distance follows */
                L.opts_changed = 0;
            }
            /* the chat screen's batch; a GuiChat or GuiSleepMP up takes the
             * input block away (allowUserInput is false) unless the batch
             * closes it, and then setIngameFocus's counter is decremented */
            int chat_up = CP.screen_chat || CP.screen_sleep;
            int chat_closes = chat_batch(&a, gin.cur_shift != 0, L.ctrl_down);
            if (chat_up && !chat_closes) a.has_in = 0;
            a.focus = L.grabbed || L.close_screen || respawned || (L.respawn && (CP.screen_gameover || CP.screen_credits)) ||
                      chat_closes;
            a.lcc = CP.screen_inventory || chat_closes ? 9999 : CP.left_click_counter > 0 ? CP.left_click_counter - 1 : 0;
            a.ctrl = L.ctrl_down;   /* GuiScreen.isCtrlKeyDown (the drop key's whole stack) */
            if (L.respawn && (CP.screen_gameover || CP.screen_credits)) a.gui_respawn = 1;
            respawn_landing = a.gui_respawn && CP.screen_gameover;
            if (L.wake && CP.screen_sleep) a.gui_wake = 1;
            L.wake = 0;
            /* the screen's queued events: its clicks and close are made
             * inside the tick, then the row goes to the tape */
            a.gui_in = &gin;
            /* a recorded session's screen clock (Minecraft.getSystemTime at
             * its screen input): the double click and the drag read it */
            for (int c = 0; c < nclock; ++c)
                if (clock_t[c] == t) { clock_last = clock_ms[c]; clock_last_t = t; }
            gin.has_clock = clock_last >= 0;
            gin.clock = clock_last + 50 * (t - clock_last_t);
            L.respawn = 0;
            L.close_screen = 0;
            int had_screen = CP.screen_inventory || CP.screen_chat || CP.screen_sleep;
            int had_inv = CP.screen_inventory;
            const struct container *had_c = cp_gui_container(&CP);
            int had_chat = CP.screen_chat || CP.screen_sleep;
            play_tick(t, &a);
            chat_after_tick();
            /* the chat screen's text: SDL's text input while one is up */
            if ((CP.screen_chat || CP.screen_sleep) && !had_chat)
            {
                chat_ptr_moved = 0;
                SDL_StartTextInput(win);
            }
            else if (!(CP.screen_chat || CP.screen_sleep) && had_chat) SDL_StopTextInput(win);
            /* the input block the tick ran: none under a container screen
             * a packet opened at the tick's start (the tape says so) */
            if (a.has_in && !CP.input_ran) a.has_in = 0;
            tape_row(t, &a);
            if (judge_ref && judge_row(t, &a)) { judge_fail = 1; L.running = 0; break; }
            /* a new container screen: no drawScreen has found its hover yet */
            if (CP.screen_inventory && (!had_inv || cp_gui_container(&CP) != had_c)) gui_input_opened(&gin);
            /* the presses nothing consumed */
            for (int k = 0; k < K_N; ++k) L.keys.presses[k] = CP.keys.presses[k];
            view_follow();
            if (CP.screen_inventory || CP.screen_gameover || CP.screen_credits || CP.screen_chat || CP.screen_sleep)
            {
                if (L.grabbed)
                {
                    L.grabbed = 0;
                    SDL_SetWindowRelativeMouseMode(win, false);
                    memset(&L.keys, 0, sizeof L.keys);   /* setIngameNotInFocus: KeyBinding.unPressAllKeys */
                    /* MouseHelper.ungrabMouseCursor warps the pointer to the
                     * window's centre: the next frame's hover is there until
                     * the pointer moves (not where it last was in a screen) */
                    struct gui_screen_layout gl = gui_screen_layout(cp_gui_container(&CP), W, H, hud_scale);
                    gin.cur_x = gui_mouse_x = centre_mouse_x(&gl);
                    gin.cur_y = gui_mouse_y = gl.sh - (H / 2) * gl.sh / H - 1;
                    script_px = (float)(W * SCALE / 2);
                    script_py = (float)(H * SCALE / 2);
                    if (!L.scripted) SDL_WarpMouseInWindow(win, script_px, script_py);
                }
            }
            else if (a.gui_close || a.gui_respawn || (had_screen && CP.in_game_has_focus && !L.grabbed))
            {
                /* a screen the tick closed without a close op (the S2E,
                 * EntityPlayerSP's portal close) took the focus back too */
                L.grabbed = 1;
                SDL_SetWindowRelativeMouseMode(win, true);
            }
            renderer_tick();
            rw_after_tick();
            /* the look the tick left */
            yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
            pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
            ++t;
            L.tick_at += 0.05;
        }
        if (!L.running) break;
        if (framed && left > 0)
        {
            printf("FAIL rawjudge rows t=%lld: the recorded client ran %d ticks in this frame, this client %d (paused)\n",
                   (long long)t, fr_ticks, fr_ticks - left);
            judge_fail = 1;
            break;
        }
        /* the recorded frame's mouse motion, after its ticks (the camera's
         * MouseHelper deltas), through the window's own events */
        if (fr_rel)
        {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = script_px;
            e.motion.y = script_py;
            e.motion.xrel = fr_rx;
            e.motion.yrel = fr_ry;
            SDL_PushEvent(&e);
            while (SDL_PollEvent(&ev)) live_event(&L, &ev);
        }

        if (fr_ptr && CP.screen_inventory)
        {
            gin.cur_x = gui_mouse_x = fr_px;
            gin.cur_y = gui_mouse_y = fr_py;
        }
        /* EntityRenderer.updateCameraAndRender: the frame's mouse through the
         * sensitivity's cube (1 at the default 0.5), then the frame */
        {
            float f1 = opt_sens * 0.6F + 0.2F, f2 = f1 * f1 * f1 * 8.0F;
            set_angles(L.mdx * f2, -L.mdy * f2 * (opt_invert ? -1.0F : 1.0F));
        }
        L.mdx = L.mdy = 0;
        float pt = framed ? fr_pt : (float)(1.0 - (L.tick_at - now) / 0.05);
        if (pt < 0) pt = 0;
        if (pt > 1) pt = 1;
        frame_now_ms = ((int64_t)t + 1) * 50;
        if (!no_draw)
        {
        struct rs_in in;
        struct rs_out o;
        build_rs(&in, pt);
        rs_compute(&in, &o);
        struct raster_live_in li = {&o, view_dim, render_rd(), cloud_tick, CP.cw_day,
                                    CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt, play_clouds};
        Uint64 f0 = SDL_GetTicksNS();
        meshed_total += raster_live_frame(RL, &li, rgb);
        frame_ms += (double)(SDL_GetTicksNS() - f0) / 1e6;
        nents = draw_hitboxes(rgb, &o, pt, NULL, assets);
        draw_tileents(rgb, &o, pt, t, assets);
        draw_block_overlays(rgb, &o);
        /* EffectRenderer.renderLitParticles + renderParticles: the light at
         * each block particle's box, then the quads, before the weather and
         * the HUD */
        surv_fx.light_world = view_world();
        particles_live_light(&surv_fx);
        {
            int aw = 0, ah = 0;
            const unsigned char *tex[4] = {raster_live_particle_tex(RL),
                                           raster_live_block_atlas(RL, &aw, &ah),
                                           NULL, raster_live_explosion_tex(RL)};
            const int tex_wh[8] = {128, 128, aw, ah, 256, 256, 128, 128};
            raster_particles_draw_live(particles_live_get(&surv_fx, 0), particles_live_count(&surv_fx),
                                       W, H, o.proj, o.mv, (const double[]){o.camx, o.camy, o.camz},
                                       o.gfogc, o.gfogs, o.gfoge, o.gfogd, o.gfogm,
                                       (const uint32_t *)o.lm,
                                       raster_live_depth_mut(RL), rgb, NULL, NULL,
                                       tex, tex_wh, raster_live_mesher(RL),
                                       pt, pyaw + (yaw - pyaw) * pt, ppitch + (pitch - ppitch) * pt);
        }
        /* sortAndRender(pass 1) after the entities, the overlays and the
         * particles: what is behind water is blended over */
        raster_live_translucent(RL, rgb);
        live_worldfx(&in, &o, pt, rgb, 1);
        live_hud(rgb, t);
        gui_input_drawn(&gin, &CP);  /* drawScreen's drag prune */
        live_screens(rgb, L.paused, gui_mouse_x, gui_mouse_y);
        live_toast(rgb, t);
        }
        gui_input_frame(&gin);   /* drawScreen's hover and the polled modifiers */
        fx_interp[0] = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt;
        fx_interp[1] = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt;
        fx_interp[2] = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt;
        fx_interp_set = 1;
        if (!no_draw)
        {
            SDL_UpdateTexture(tex, NULL, rgb, W * 3);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        }

        ++frames;
        if (now - fps_at >= 0.5)
        {
            fps = (float)(frames / (now - fps_at));
            char title[256];
            snprintf(title, sizeof title,
                     "netherite (C)  %.0f fps  %.1f ms/frame  %d remeshed  %d entities  t=%lld  %.1f %.1f %.1f  hp %.0f  slot %d%s",
                     fps, frame_ms / frames, meshed_total, nents, (long long)t, CP.e.pos_x, CP.e.pos_y, CP.e.pos_z,
                     CP.sv.health, CP.hotbar + 1, L.grabbed ? "" : "  (click to play)");
            meshed_total = 0;
            SDL_SetWindowTitle(win, title);
            frames = 0;
            frame_ms = 0;
            fps_at = now;
        }
    }
    if (script_file) fclose(script_file);
    if (judge_ref)
    {
        tape_close(&judge_tp);
        if (!judge_fail && judge_rows == 0) { printf("FAIL rawjudge rows: no row played\n"); judge_fail = 1; }
        if (!judge_fail)
        {
            printf("PASS rawjudge rows: %ld rows from the recorded raw events, each act and state as Java's\n", judge_rows);
            printf("PASS rawjudge gui: %ld ticks of screen ops (%ld clicks) as Java's screens sent them\n",
                   judge_gui_ticks, judge_clicks);
        }
    }

    fclose(tape_out);
    fprintf(stderr, "play: %lld ticks played, tape %s\n", (long long)(t - S.tick), out);
    fprintf(stderr, "play: check it against Java with: csrc/play/check.sh %s\n", out);
    free(rgb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    raster_live_free(RL);
    return judge_fail ? 3 : 0;
}

#ifdef PLAY_VIEW
/* ------------------------------------------------------- the library (playview.h)
 *
 * The frame the shots draw (--shots with --obs-dump: judge_shot_draw), for a
 * pool environment's session: pv_open is play_open's and main's setup of
 * the view, tick_begin and tick_end are play_tick around the pool's own
 * session_tick followed by the replay loop's view_follow, renderer_tick and
 * rw_after_tick, and frame is judge_shot_draw's world passes at partial
 * tick 1.0 with the recorder on. Nothing here touches game state that
 * play itself does not. */
static struct play_tick_carry pv_carry;
static struct raster_rec *pv_rec;
static unsigned char *pv_img;
static float *pv_depth;

static int pv_open(struct session *ss, const struct pv_config *c, char *err, size_t n)
{
    pv_ss = ss;
    W = c->w;
    H = c->h;
    play_view_rd = c->rd;
    play_hide_gui = c->hide_gui;
    const char *assets = c->assets ? c->assets : "out/java/render/forest-fast";
    view_assets = mob_assets = hand_assets = assets;
    hud_assets = "out/java/render/hud_mixed";
    fancy_assets = recorded_fancy(assets);
    view_threads = 1;
    play_dmesh = c->device_mesh;
    play_mesh_check = c->mesh_check;
    play_prec = c->render_prec;
    opt_gamma = c->gamma;
    raster_entity_set_prec(play_prec);
    raster_hud_shared_json = c->shared_json;
    if (!SS.server_rows)
    {
        snprintf(err, n, "not a whole-server snapshot (onlyPlayers is not 1)");
        return 0;
    }
    /* play_open */
    live_seed = S.seed;
    det_pin_init(&pin_flicker, DET_PIN_FLICKER, S.seed);
    det_pin_init(&pin_item, DET_PIN_ITEM, S.seed);
    SS.hooks.on_chunk = rw_on_chunk;
    SS.hooks.world_changed = live_world_changed;
    SS.hooks.before_client_tick = live_before_client_tick;
    SS.hooks.after_world_tick = live_after_world_tick;
    /* tape_begin: the header's options */
    {
        const struct jval *opts = json_get(SS.hdr, "options");
        int64_t val;
        if (json_int(json_get(opts, "rd"), &val)) play_rd = (int)val;
        if (json_int(json_get(opts, "bob"), &val)) play_bob = (int)val;
        if (json_int(json_get(opts, "clouds"), &val)) play_clouds = (int)val;
        if (json_int(json_get(opts, "fancy"), &val)) play_fancy = (int)val;
    }
    /* main */
    CP.update_renderer = live_update_renderer;
    view_dim = SP.dimension;
    if (!rw_build())
    {
        snprintf(err, n, "the render world has no chunks");
        return 0;
    }
    hud_update_counter = (int)S.tick;
    RL = raster_live_new(assets, &RW, W, H, view_threads);
    if (play_dmesh) raster_live_device_mesh(RL, play_dmesh);
    raster_live_set_prec(RL, play_prec);
    raster_live_mesh_check(RL, play_mesh_check);
    raster_rec_verify(play_mesh_check);
    play_anim_open(assets);
    surv_fx.mesher = raster_live_mesher(RL);
    light_table();
    yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
    pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
    fc1 = fc2 = 0.0F;
    {
        const struct surv_stack *held = &CP.sv.inv[CP.hotbar];
        if (held->count > 0)
        {
            equip.present = 1;
            equip.item = held->item;
            equip.damage = held->damage;
            equip.gen = held->gen;
            equip.slot = CP.hotbar;
        }
    }
    return 1;
}

static void pv_tick_begin(int64_t t, const struct act *a)
{
    play_tick_begin(t, a, &pv_carry);
}

static void pv_tick_end(int64_t t, const struct act *a)
{
    play_tick_end(t, a, &pv_carry);
    view_follow();
    renderer_tick();
    rw_after_tick();
    yaw = CP.rotation_yaw; pitch = CP.rotation_pitch;
    pyaw = CP.prev_rotation_yaw; ppitch = CP.prev_rotation_pitch;
}

static int pv_frame(struct raster_obs *o, int record_only, unsigned char *rgb)
{
    const float pt = 1.0F;
    frame_now_ms = ((int64_t)live_t + 1) * 50;
    unsigned char *img = rgb;
    if (!img)
    {
        if (!pv_img) pv_img = malloc((size_t)W * H * 3);
        img = pv_img;
    }
    struct rs_in in;
    struct rs_out ro;
    build_rs(&in, pt);
    rs_compute(&in, &ro);
    struct raster_live_in li = {&ro, view_dim, render_rd(), cloud_tick, CP.cw_day,
                                CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt, play_clouds};
    raster_rec_set_only(record_only);
    raster_live_frame(RL, &li, img);
    if (!pv_rec) pv_rec = play_rec_new();
    raster_rec_begin(pv_rec);
    shot_world(img, pt, 0, view_assets, 0, NULL);
    raster_rec_end();
    raster_rec_set_only(0);
    int rc = raster_live_obs(RL, o);
    if (!rc) raster_rec_attach(pv_rec, o);
    /* renderParticles leaves EntityFX.interpPos at this frame's camera, as
     * the shots loop keeps it */
    fx_interp[0] = CP.e.prev_pos_x + (CP.e.pos_x - CP.e.prev_pos_x) * pt;
    fx_interp[1] = CP.e.prev_pos_y + (CP.e.pos_y - CP.e.prev_pos_y) * pt;
    fx_interp[2] = CP.e.prev_pos_z + (CP.e.pos_z - CP.e.prev_pos_z) * pt;
    fx_interp_set = 1;
    return rc;
}

static void pv_render(const struct raster_obs *o, unsigned char *rgb)
{
    if (!pv_depth) pv_depth = malloc((size_t)W * H * sizeof *pv_depth);
    raster_obs_render(o, rgb, pv_depth);
}

static void pv_mesh_resync(void)
{
    if (RL) raster_live_mesh_resync(RL);
}

static void pv_frame_stats(struct pv_frame_stats *o)
{
    const struct raster_live_stats *st = raster_live_stats(RL);
    uint64_t tc = 0, tb = 0, bc = 0, bb = 0;
    raster_rec_verify_stats(&tc, &tb);
    raster_live_key_stats(RL, &bc, &bb);
    *o = (struct pv_frame_stats){st->meshed, st->reused, st->empty, st->draws, st->checked, st->check_bad, tc, tb, bc, bb, st->feed_ms};
}

__attribute__((visibility("default"))) const struct pv_api pv_api = {
    PV_API_VERSION, pv_open, pv_tick_begin, pv_tick_end, pv_frame, pv_render, pv_frame_stats, pv_mesh_resync,
};
#endif
