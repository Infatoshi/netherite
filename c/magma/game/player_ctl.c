/* game/player_ctl.c - PLAYER-CTL module: live composition of verified mc-sim kernels.
 *
 * Drives ONE player tick over a region-centered raw-Chunk `window` by REUSING:
 *   psv_physics_tick, psv_raycast, psv_get/set_block, player_vitals,
 *   player_break (progressive dig), item_block_place (orientation meta),
 *   interact_blocks (door/lever/button/... toggles), container_click (inventory).
 *
 * WINDOW MUTATION: single window; physics reads pre-edit; break/place/interact write
 * into the window for next-tick consistency; caller applies WORLD edits via
 * gm_world_set_block_meta.
 */
#include "game/player_ctl.h"
#include "game/sel_box.h"
#include "player_vitals.h"
#include "player_break.h"
#include "item_block_place.h"
#include "interact_blocks.h"
#include "container_click.h"
#include "items_tools_armor.h"
#include "mc_blocks.h"
#include <math.h>
#include <limits.h>

/* Progressive dig state (single local player): mirrors vanilla PlayerControllerMP.
 * s_dig_progress = curBlockDamageMP, s_dig_h* = currentBlock, s_dig_hitting =
 * isHittingBlock, s_dig_delay = blockHitDelay (5 ticks after every damage-path
 * break), s_atk_prev = attack key edge (press tick = clickMouse -> clickBlock). */
static float s_dig_progress;

/* EntityRenderer.fovModifierHand (client render state, not physics). */
static float s_fov_hand = 1.0f;
/* ticks the bow has been drawn (ItemBow active use; getItemInUseMaxCount). */
static int   s_bow_ticks;
static int   s_dig_hx = INT_MIN, s_dig_hy, s_dig_hz;
static int   s_dig_hitting;
static int   s_dig_delay;
static int   s_atk_prev;
/* rightClickMouse state: s_rc_delay = Minecraft.rightClickDelayTimer (set to 4
 * on every fire, decremented each tick before the fire checks), s_use_prev =
 * use key edge (press edge fires regardless of the timer; held use re-fires
 * only when the timer hits 0). */
static int   s_rc_delay;
static int   s_use_prev;
/* Hurt velocity reset: any successful player damage sets velocityChanged and
 * the server self-sends SPacketEntityVelocity. The integrated-server player's
 * horizontal motion is normally zero, but a sprint jump leaves a 0.91-decayed
 * kick there while client packets move its position. Shadow that server-only
 * motion so fall damage during a sprint jump restores the packet value, not
 * zero. Mob knockback is applied directly elsewhere and stays untouched. */
static int   s_hurt_vel_reset;
static double s_server_motion_x, s_server_motion_z;
static int   s_eat_ticks, s_eat_item;
/* Active hand use for viewmodel (EAT/DRINK/BLOCK). BOW uses s_bow_ticks. */
static int   s_use_action;      /* 0 none, 1 eat/drink, 2 block */
static int   s_use_remaining;
static int   s_use_max;

/* Optional cursor for live inventory composition (hotbar is IsrInv slots 0..8). */
static ICStack s_cursor;

/* VANILLA vitals for the game (verified player_vitals oracle). */
static void gm_vitals_apply(PvStats *vit, PsvPlayer *pl, GmAction act,
                            int was_air, double prev_min_y,
                            double dx, double dy, double dz,
                            int in_water_pre, int eye_water_post, int land_jump)
{
    McEntity *e = &pl->ent;

    /* EntityLivingBase's elytra wall branch attacks immediately after move.
     * DamageSource.FLY_INTO_WALL is unblockable (setDamageBypassesArmor). */
    if (pl->elytra_wall_damage > 0.0f)
        pv_attack(vit, pl->elytra_wall_damage);

    /* EntityPlayer.addMovementStat: exhaustion is charged per WHOLE cm
     * (Math.round(MathHelper.sqrt(d)*100F), float sqrt), diving (eyes under,
     * 3D distance) and swimming (inWater, 2D) at 0.0001/cm, sprinting only
     * while onGround at 0.001/cm; plain walking/sneaking cost 0. Jump
     * exhaustion (EntityPlayer.jump: 0.2 sprint / 0.05) fires only on an
     * actual LAND jump - handleJumpWater/Lava never charges. Found at tape
     * 20260712T055346Z t1488: magma's food hit 19 early because bouncing on
     * the pond floor with jump held charged 0.05/tick and mid-air sprint
     * distance was billed at the ground rate. */
    if (eye_water_post) {
        int i = (int)floorf(sqrtf((float)(dx * dx + dy * dy + dz * dz)) * 100.0f + 0.5f);
        if (i > 0) pv_add_exhaustion(vit, 0.01f * (float)i * 0.01f);
    } else if (in_water_pre) {
        int j = (int)floorf(sqrtf((float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (j > 0) pv_add_exhaustion(vit, 0.01f * (float)j * 0.01f);
    } else if (e->onGround && act.sprint) {
        int k = (int)floorf(sqrtf((float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (k > 0) pv_add_exhaustion(vit, 0.1f * (float)k * 0.01f);
    }
    if (land_jump) pv_add_exhaustion(vit, act.sprint ? 0.2f : 0.05f);

    if (!e->onGround) {
        double dropped = prev_min_y - e->box.minY;
        if (dropped > 0.0 && !pl->reset_fall_distance)
            pl->fall_distance += (float)dropped;
    } else if (was_air && pl->fall_distance > 0.0f) {
        pv_fall_damage(vit, pl->fall_distance);
    }
    if (e->onGround) pl->fall_distance = 0.0f;

    pv_on_update(vit);
    pl->health = vit->health;
    pl->food   = (float)vit->foodLevel;
}

/* psv_get_meta now lives in player_survival.h beside psv_get_block. */

static void psv_set_state(Chunk *chunks, int wx, int wy, int wz, int id, int meta) {
    int lx, lz, ci;
    if (wy < 0 || wy > 255) return;
    ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci < 0) return;
    mc_set(&chunks[ci], lx, wy, lz, mc_state(id, meta & 15));
}

static int ib_is_interactable(int id) {
    return id == IB_WOODEN_DOOR || id == IB_IRON_DOOR || id == IB_LEVER
        || id == IB_STONE_PLATE || id == IB_WOODEN_PLATE || id == IB_STONE_BUTTON
        || id == IB_TRAPDOOR || id == IB_FENCE_GATE || id == IB_WOODEN_BUTTON
        || id == IB_LIGHT_PLATE || id == IB_HEAVY_PLATE || id == IB_IRON_TRAPDOOR;
}

/* Hit face from the cell adjacent to the selected AABB face (EnumFacing D-U-N-S-W-E). */
static int face_from_adj(int hx, int hy, int hz, int ax, int ay, int az) {
    if (ax < hx) return IBP_WEST;
    if (ax > hx) return IBP_EAST;
    if (ay < hy) return IBP_DOWN;
    if (ay > hy) return IBP_UP;
    if (az < hz) return IBP_NORTH;
    if (az > hz) return IBP_SOUTH;
    return IBP_NORTH;
}

static int yaw_to_quad(float yaw_deg) {
    /* Entity.getHorizontalFacing: floor(yaw * 4/360 + 0.5) & 3 */
    double y = fmod((double)yaw_deg, 360.0);
    if (y < 0.0) y += 360.0;
    return ((int)floor(y * 4.0 / 360.0 + 0.5)) & 3;
}

/* vanilla Block.isReplaceable: air, liquids, tallgrass/deadbush, fire, snow layer.
 * Placing into one of these destroys it (no drop without shears). */
static int psv_replaceable(int id) {
    return id == 0 || (id >= 8 && id <= 11) || id == 31 || id == 32 ||
           id == 51 || id == 78;
}

/* BlockTorch.canPlaceBlockAt/getStateForPlacement. The pure orientation table
 * deliberately omits world support checks, but the live path must reject a
 * repeated use that tries to stack a torch on another torch (the canonical
 * tape holds use for several ticks). `face` points from the clicked support
 * block into the placement cell. */
static int torch_placement_meta(const Chunk *w, int x, int y, int z, int face) {
    int support = 0;
    switch (face) {
    case IBP_UP:    support = psv_solid(psv_get_block(w, x, y - 1, z)); break;
    case IBP_NORTH: support = psv_solid(psv_get_block(w, x, y, z + 1)); break;
    case IBP_SOUTH: support = psv_solid(psv_get_block(w, x, y, z - 1)); break;
    case IBP_WEST:  support = psv_solid(psv_get_block(w, x + 1, y, z)); break;
    case IBP_EAST:  support = psv_solid(psv_get_block(w, x - 1, y, z)); break;
    default: break; /* torches cannot attach to ceilings */
    }
    if (support) return ibp_meta_torch(face);

    /* BlockTorch's horizontal fallback iterates EnumFacing value order. */
    if (psv_solid(psv_get_block(w, x, y, z + 1))) return ibp_meta_torch(IBP_NORTH);
    if (psv_solid(psv_get_block(w, x, y, z - 1))) return ibp_meta_torch(IBP_SOUTH);
    if (psv_solid(psv_get_block(w, x + 1, y, z))) return ibp_meta_torch(IBP_WEST);
    if (psv_solid(psv_get_block(w, x - 1, y, z))) return ibp_meta_torch(IBP_EAST);
    if (psv_solid(psv_get_block(w, x, y - 1, z))) return ibp_meta_torch(IBP_UP);
    return -1;
}

/* Entity.isInsideOfMaterial(WATER): the eye sits below the liquid surface of
 * its cell. BlockLiquid.getLiquidHeightPercent: falling (meta>=8) counts as a
 * full block; else (meta+1)/9, surface at (y+1) - percent. */
static int eye_in_water(const Chunk *w, const PsvPlayer *pl) {
    double eye_y = pl->ent.posY + psv_player_eye_height(pl);
    int x = mc_floor(pl->ent.posX), y = mc_floor(eye_y), z = mc_floor(pl->ent.posZ);
    int id = psv_get_block(w, x, y, z);
    if (id != 8 && id != 9) return 0;
    int m = psv_get_meta(w, x, y, z);
    if (m >= 8) m = 0;
    return eye_y < (double)(y + 1) - (double)(m + 1) / 9.0;
}

static int bucket_raycast(const Chunk *w,const McSinTable *st,const PsvPlayer *pl,
                          int *hx,int *hy,int *hz){
    float f=mc_cos(st,-pl->yaw*0.017453292f-3.1415927f);
    float f1=mc_sin(st,-pl->yaw*0.017453292f-3.1415927f);
    float f2=-mc_cos(st,-pl->pitch*0.017453292f),f3=mc_sin(st,-pl->pitch*0.017453292f);
    double dx=(double)(f1*f2),dy=(double)f3,dz=(double)(f*f2);
    double ex=pl->ent.posX,ey=pl->ent.posY+psv_player_eye_height(pl),ez=pl->ent.posZ;
    int lx=mc_floor(ex),ly=mc_floor(ey),lz=mc_floor(ez);
    for(double t=PSV_RAY_DT;t<=PSV_REACH;t+=PSV_RAY_DT){
        int x=mc_floor(ex+dx*t),y=mc_floor(ey+dy*t),z=mc_floor(ez+dz*t);
        if(x==lx&&y==ly&&z==lz)continue;
        lx=x;ly=y;lz=z;
        int id=psv_get_block(w,x,y,z),meta=psv_get_meta(w,x,y,z);
        if((id==8||id==9||id==10||id==11)&&meta==0){*hx=x;*hy=y;*hz=z;return id;}
        if(psv_solid(id))return 0;
    }return 0;
}

static void emit_edit(GmBlockEdit *edits, int *ne, int max_edits,
                      int ox, int oy, int oz, int lx, int ly, int lz, int id, int meta,
                      int drop_id, int drop_count, int drop_meta) {
    if (*ne >= max_edits) return;
    edits[*ne].wx = ox + lx;
    edits[*ne].wy = oy + ly;
    edits[*ne].wz = oz + lz;
    edits[*ne].id = id;
    edits[*ne].meta = meta & 15;
    edits[*ne].drop_id = drop_id;
    edits[*ne].drop_count = drop_count;
    edits[*ne].drop_meta = drop_meta & 15;
    (*ne)++;
}

/* Route-relevant vanilla harvest mapping. This deliberately starts narrow; an
 * unsupported block still breaks but yields no fabricated item. */
static void harvest_drop(int block_id, int block_meta, int tool_id,int wx,int wy,int wz,
                         int *item, int *count, int *meta) {
    *item = 0; *count = 0; *meta = 0;
    if (!pb_can_harvest(tool_id, block_id)) return;
    switch (block_id) {
        case 1:
            /* BlockStone.getItemDropped/damageDropped: plain stone becomes
             * cobblestone; granite/diorite/andesite retain stone metadata. */
            *item = block_meta >= 1 && block_meta <= 6 ? 1 : 4;
            *count = 1;
            *meta = *item == 1 ? block_meta : 0;
            break;
        case 2:  *item = 3;  *count = 1; break; /* grass -> dirt */
        case 3:  *item = 3;  *count = 1; *meta = block_meta & 3; break;
        case 4:  *item = 4;  *count = 1; break;
        case 12: *item = 12; *count = 1; *meta = block_meta & 1; break;
        case 13: {
            u32 h=(u32)wx*73428767u^(u32)wy*912931u^(u32)wz*19349663u;
            h^=h>>13;h*=0x85ebca6bu;h^=h>>16;
            *item=(h%10u)==0u?318:13;*count=1;break;
        }
        case 14: *item = 14; *count = 1; break; /* gold ore -> ore block */
        case 15: *item = 15; *count = 1; break; /* iron ore -> ore block */
        case 16: *item = 263; *count = 1; break;/* coal ore -> coal */
        case 17: *item = 17; *count = 1; *meta = block_meta & 3; break;
        case 49: *item = 49; *count = 1; break; /* obsidian */
        case 54: *item = 54; *count = 1; break; /* chest block item */
        case 56: *item = 264; *count = 1; break;/* diamond ore -> diamond */
        default: break;
    }
}

/* onPlayerDestroyBlock slice: drops + tool wear + window clear + world edit. */
static void dig_destroy(Chunk *window, PsvPlayer *pl, int hx, int hy, int hz,
                        int bid, int bmeta, int ox, int oy, int oz,
                        GmBlockEdit *edits, int *ne, int max_edits)
{
    ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
    int drop_id, drop_count, drop_meta;
    harvest_drop(bid, bmeta, held.item, hx + ox, hy + oy, hz + oz,
                 &drop_id, &drop_count, &drop_meta);
    if (!isr_is_empty(&held)) {
        ITAStack tool = ita_mk(held.item, held.meta);
        ita_on_block_destroyed(&tool, bid);
        int max_damage = ita_stack_max_damage(&tool);
        if (max_damage > 0) {
            if (tool.damage > max_damage)
                (void)isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
            else {
                held.meta = tool.damage;
                isr_set_stack(&pl->inv, pl->inv.current_item, held);
            }
        }
    }
    psv_set_state(window, hx, hy, hz, BLK_AIR, 0);
    pl->break_events++;
    emit_edit(edits, ne, max_edits, ox, oy, oz, hx, hy, hz, 0, 0,
              drop_id, drop_count, drop_meta);
}

void gm_player_tick(struct Chunk *window_, const struct McSinTable *st_,
                    struct PsvPlayer *pl_, struct PvStats *vitals_, GmAction act,
                    int ox, int oy, int oz,
                    GmBlockEdit *edits, int *nedits, int max_edits)
{
    Chunk            *window = (Chunk *)window_;
    const McSinTable *st     = (const McSinTable *)st_;
    PsvPlayer        *pl     = (PsvPlayer *)pl_;
    PvStats          *vit    = (PvStats *)vitals_;

    McAABB blocks[PSV_MAX_BLOCKS];
    int ne = 0;

    /* hotbar selection (keys 1-9 / scroll) */
    if (act.hotbar_sel >= 0 && act.hotbar_sel <= 8)
        pl->inv.current_item = act.hotbar_sel;

    /* Inventory slotClick moved UP to gm_runtime_tick -> gm_container_click
     * (full 36-slot + grid/result/furnace ids; real drop entities). The 9-slot
     * gm_player_inv_click below stays as a component-level helper only. */

    PsvAction a;
    a.forward = act.forward;
    /* GmAction.strafe +1 = D/right; vanilla moveStrafe +1 = LEFT */
    a.strafe  = -act.strafe;
    /* MovementInputFromOptions.updatePlayerMoveState: sneaking scales both
     * move inputs by 0.3 (before EntityLivingBase's 0.98). */
    if (act.sneak) {
        a.forward = (float)((double)a.forward * 0.3);
        a.strafe  = (float)((double)a.strafe  * 0.3);
    }
    a.sneak   = act.sneak;
    a.jump    = act.jump;

    pl->yaw   += act.dyaw;
    pl->pitch += act.dpitch;
    if (pl->pitch >  89.0f) pl->pitch =  89.0f;
    if (pl->pitch < -89.0f) pl->pitch = -89.0f;
    a.yaw   = pl->yaw;
    a.pitch = pl->pitch;

    a.do_break = 0;   /* progressive dig uses attack held below */
    a.do_place = 0;
    a.attack   = act.attack;

    /* vanilla sprint state machine */
    {
        if (pl->sprint_toggle_timer > 0) pl->sprint_toggle_timer--;
        int   flag1 = pl->prev_sneak;
        int   flag2 = pl->prev_move_forward >= 0.8f;
        float mf = act.sneak ? (float)((double)act.forward * 0.3) : act.forward;
        int   flag4 = pl->food > 6.0f;
        if (pl->ent.onGround && !flag1 && !flag2 && mf >= 0.8f && !pl->sprinting && flag4) {
            if (pl->sprint_toggle_timer <= 0 && !act.sprint)
                pl->sprint_toggle_timer = 7;
            else
                pl->sprinting = 1;
        }
        if (!pl->sprinting && mf >= 0.8f && flag4 && act.sprint) pl->sprinting = 1;
        if (pl->sprinting && (mf < 0.8f || pl->ent.collidedHorizontally || !flag4))
            pl->sprinting = 0;
        pl->prev_move_forward = mf;
        pl->prev_sneak = act.sneak;
        act.sprint = pl->sprinting;
    }
    a.sprint = act.sprint;

    /* EntityRenderer.updateFovModifierHand: FOV eases toward the player's
     * fov modifier at 0.5/tick. AbstractClientPlayer.getFovModifier =
     * (movement_speed_attr / walk_speed + 1) / 2; the sprint attribute
     * modifier (+30% multiply_total) makes that 1.15 while sprinting, 1.0
     * otherwise (no speed potions / creative flight / bow draw simulated).
     * Sprint-into-a-wall oscillates the flag via collidedHorizontally above,
     * which is exactly the vanilla FOV pumping artifact. */
    {
        float target = pl->sprinting ? 1.15f : 1.0f;
        /* getFovModifier bow branch: while the bow is drawn, the world FOV
         * zooms by 1 - min(1, useTicks/20)^2 * 0.15 (the hand projection
         * stays at 70 - getFOVModifier(pt,false) skips fovModifierHand).
         * Vanilla activation also requires an arrow in the inventory;
         * magma only mirrors the hotbar, so trust the recorded inputs
         * (the oracle only holds a draw it could start). */
        {
            ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
            if (act.use && held.item == 261) {
                float f1 = (float)s_bow_ticks / 20.0f;
                f1 = f1 > 1.0f ? 1.0f : f1 * f1;
                target *= 1.0f - f1 * 0.15f;
                if (s_bow_ticks < 72000) ++s_bow_ticks;
            } else {
                s_bow_ticks = 0;
            }
        }
        s_fov_hand += (target - s_fov_hand) * 0.5f;
        if (s_fov_hand > 1.5f) s_fov_hand = 1.5f;
        if (s_fov_hand < 0.1f) s_fov_hand = 0.1f;
    }

    int    was_air    = !pl->ent.onGround;
    double prev_min_y =  pl->ent.box.minY;
    double pre_x      =  pl->ent.posX;
    double pre_y      =  pl->ent.posY;
    double pre_z      =  pl->ent.posZ;

    /* SPacketEntityVelocity after damage: overwrite motion with the SERVER
     * player's motion BEFORE this tick's move. Server X/Z motion is zero (no
     * input runs there); server motionY is its own gravity value, which for
     * the on-ground fall-damage case equals the client's resting -0.0784, so
     * motionY is left untouched (zeroing it flips onGround: a 0-length y move
     * makes Entity.moveEntity set onGround=false - seen as an og divergence
     * at t712 when motionY was reset to 0 here). Found at tape
     * 20260712T055346Z t712: after the t711 fall-damage landing (hp 20->17)
     * the oracle's t712 move is accel-only (dx=0.084872) while magma kept
     * vx=0.157733; magma_dx - oracle_dx equalled the retained motion
     * exactly. */
    if (s_hurt_vel_reset) {
        /* SPacketEntityVelocity quantizes each component through
         * (int)(motion * 8000) and the client divides by 8000. */
        pl->ent.motionX = (double)(int)(s_server_motion_x * 8000.0) / 8000.0;
        pl->ent.motionZ = (double)(int)(s_server_motion_z * 8000.0) / 8000.0;
        s_hurt_vel_reset = 0;
    }

    /* ---- progressive dig BEFORE the move (vanilla Minecraft.runTick: clickMouse +
     * sendClickBlockToController run before the world/entity tick, so the ray uses
     * the PRE-move pose and a break is visible to the same tick's physics).
     * State machine = PlayerControllerMP: press tick = clickBlock (acquire, damage
     * 0 unless instant) THEN onPlayerDamageBlock (damage); held ticks damage-only;
     * a target change costs an acquire tick; every damage-path break arms
     * blockHitDelay=5 which swallows the next 5 ticks entirely. Found via tape
     * 20260712T055346Z t438-562 hold-dig: without the delay magma broke a 6th
     * block (49,71,164) the oracle never finished, then walked through the space
     * where the oracle got clamped (t579). */
    int use_gate_hitting = s_dig_hitting; /* isHittingBlock as rightClickMouse
        sees it: after clickMouse (attack press) but BEFORE the damage phase /
        release reset (vanilla runTick order). */
    if (act.attack) {
        int press = !s_atk_prev;
        int hx, hy, hz, ax, ay, az;
        /* dig uses PSV_REACH (5.0); outline uses survival 4.5 via gm_raycast_sel */
        int r = gm_raycast_sel_reach(window, st, pl, PSV_REACH,
                                     &hx, &hy, &hz, &ax, &ay, &az);
        if (r >= 0) {
            int bid = psv_get_block(window, hx, hy, hz);
            BptProps bp = mc_bpt_props(bid);
            /* any ray-targetable block digs (torch/plants too), not just
             * full-cube solids; liquids/air never reach here (ray skips them) */
            if (bid != BLK_AIR && bp.hardness >= 0.0f) {
                ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
                PbInput pin;
                pin.block_id = bid;
                pin.block_meta = psv_get_meta(window, hx, hy, hz);
                pin.tool_id = held.item;
                pin.tool_meta = held.meta;
                pin.efficiency = 0;
                pin.haste_amp = -1;
                pin.fatigue_amp = -1;
                pin.in_water = eye_in_water(window, pl);
                pin.aqua_affinity = 0;
                pin.on_ground = pl->ent.onGround;
                pin.creative = 0;
                float rel = pb_relative_hardness(&pin);
                if (press &&
                    (!s_dig_hitting || hx != s_dig_hx || hy != s_dig_hy || hz != s_dig_hz)) {
                    /* clickMouse -> clickBlock */
                    if (rel >= 1.0f) {   /* instant break: no blockHitDelay */
                        dig_destroy(window, pl, hx, hy, hz, bid, pin.block_meta,
                                    ox, oy, oz, edits, &ne, max_edits);
                        bid = BLK_AIR;   /* sendClickBlockToController sees air, skips */
                    } else {
                        s_dig_hitting = 1;
                        s_dig_hx = hx; s_dig_hy = hy; s_dig_hz = hz;
                        s_dig_progress = 0.0f;
                        use_gate_hitting = 1;
                    }
                }
                /* sendClickBlockToController -> onPlayerDamageBlock */
                if (bid != BLK_AIR) {
                    if (s_dig_delay > 0) {
                        --s_dig_delay;
                    } else if (hx == s_dig_hx && hy == s_dig_hy && hz == s_dig_hz) {
                        /* isHittingPosition: accrue curBlockDamageMP */
                        s_dig_progress += rel;
                        if (s_dig_progress >= 1.0f) {
                            s_dig_hitting = 0;
                            dig_destroy(window, pl, hx, hy, hz, bid, pin.block_meta,
                                        ox, oy, oz, edits, &ne, max_edits);
                            s_dig_progress = 0.0f;
                            s_dig_delay = 5;
                        }
                    } else {
                        /* clickBlock: target changed mid-hold */
                        if (rel >= 1.0f) {   /* instant break: no blockHitDelay */
                            dig_destroy(window, pl, hx, hy, hz, bid, pin.block_meta,
                                        ox, oy, oz, edits, &ne, max_edits);
                        } else {
                            s_dig_hitting = 1;
                            s_dig_hx = hx; s_dig_hy = hy; s_dig_hz = hz;
                            s_dig_progress = 0.0f;
                        }
                    }
                }
            } else {
                s_dig_hitting = 0;
                s_dig_hx = INT_MIN;
                s_dig_progress = 0.0f;
            }
        } else {
            /* MISS: resetBlockRemoving (blockHitDelay persists) */
            s_dig_hitting = 0;
            s_dig_hx = INT_MIN;
            s_dig_progress = 0.0f;
        }
    } else {
        /* attack released: resetBlockRemoving (blockHitDelay persists) */
        s_dig_hitting = 0;
        s_dig_hx = INT_MIN;
        s_dig_progress = 0.0f;
    }
    s_atk_prev = act.attack;

    /* ---- rightClickMouse (use) BEFORE the move (vanilla runTick fires it
     * between clickMouse and sendClickBlockToController; use_gate_hitting
     * preserves that gate point). Timer semantics: rightClickDelayTimer is
     * decremented every tick, a press edge fires regardless of it, held use
     * re-fires only when it reaches 0, every fire re-arms it to 4, and the
     * whole call is skipped while isHittingBlock. Scripted do_place keeps its
     * one-shot behavior. Found at tape 20260712T055346Z t611/t627 nerd-pole:
     * the t611 mid-jump place must FAIL (placement cube intersects the
     * pre-move player bb at y=72.75), the held t612-613 ticks must not
     * re-fire (timer=4), and the t627 press must place (bb clear at
     * y=73.25) so the player lands on the new block at t629. */
    if (s_rc_delay > 0) --s_rc_delay;
    {
        int use_fire = act.use && (!s_use_prev || s_rc_delay == 0) &&
                       !use_gate_hitting;
        if (use_fire) s_rc_delay = 4;
        s_use_prev = act.use;
        if (use_fire) act.do_place = 1;
    }

    /* ---- use: interact toggle OR place with orientation meta ---- */
    if (act.do_place) {
        ICStack held0=isr_get_stack(&pl->inv,pl->inv.current_item);
        if(held0.item==325){
            int bx,by,bz,bid=bucket_raycast(window,st,pl,&bx,&by,&bz);
            if(bid){
                psv_set_state(window,bx,by,bz,0,0);
                isr_set_stack(&pl->inv,pl->inv.current_item,ic_mk(bid==8||bid==9?326:327,1,0));
                emit_edit(edits,&ne,max_edits,ox,oy,oz,bx,by,bz,0,0,0,0,0);
                goto use_done;
            }
        }
        int hx, hy, hz, ax, ay, az;
        int r = gm_raycast_sel_reach(window, st, pl, PSV_REACH,
                                     &hx, &hy, &hz, &ax, &ay, &az);
        if (r >= 0) {
            int hit_id = psv_get_block(window, hx, hy, hz);
            int hit_meta = psv_get_meta(window, hx, hy, hz);
            if (ib_is_interactable(hit_id)) {
                IbCase ic;
                ic.block_id = hit_id;
                ic.meta_in = hit_meta;
                ic.action = IB_ACT_CLICK;
                ic.arg0 = yaw_to_quad(pl->yaw); /* fence-gate facing */
                IbResult ir = ib_apply(&ic);
                if (ir.accepted) {
                    psv_set_state(window, hx, hy, hz, hit_id, ir.meta_out);
                    emit_edit(edits, &ne, max_edits, ox, oy, oz, hx, hy, hz,
                              hit_id, ir.meta_out, 0, 0, 0);
                }
            } else if (r == 1) {
                /* place against hit face into air cell */
                ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
                if ((held.item==326||held.item==327) && psv_replaceable(psv_get_block(window,ax,ay,az))) {
                    int fluid=held.item==326?8:10;
                    psv_set_state(window,ax,ay,az,fluid,0);
                    isr_set_stack(&pl->inv,pl->inv.current_item,ic_mk(325,1,0));
                    emit_edit(edits,&ne,max_edits,ox,oy,oz,ax,ay,az,fluid,0,0,0,0);
                } else if(held.item==355&&psv_get_block(window,ax,ay,az)==BLK_AIR){
                    int q=yaw_to_quad(pl->yaw),dx[4]={0,-1,0,1},dz[4]={1,0,-1,0};
                    int hx2=ax+dx[q],hz2=az+dz[q];
                    if(psv_get_block(window,hx2,ay,hz2)==BLK_AIR){
                        psv_set_state(window,ax,ay,az,26,q);
                        psv_set_state(window,hx2,ay,hz2,26,q|8);
                        (void)isr_decr_stack_size(&pl->inv,pl->inv.current_item,1);
                        emit_edit(edits,&ne,max_edits,ox,oy,oz,ax,ay,az,26,q,0,0,0);
                        emit_edit(edits,&ne,max_edits,ox,oy,oz,hx2,ay,hz2,26,q|8,0,0,0);
                    }
                } else if (!isr_is_empty(&held) && psv_get_block(window, ax, ay, az) == BLK_AIR && held.item==259) {
                    /* ItemFlintAndSteel.onItemUse: light the adjacent air cell.
                     * Runtime applies verified BlockPortal frame detection after this edit. */
                    psv_set_state(window,ax,ay,az,51,0);
                    held.meta++;
                    if(held.meta>64)(void)isr_decr_stack_size(&pl->inv,pl->inv.current_item,1);
                    else isr_set_stack(&pl->inv,pl->inv.current_item,held);
                    emit_edit(edits,&ne,max_edits,ox,oy,oz,ax,ay,az,51,0,0,0,0);
                } else if (!isr_is_empty(&held) && psv_replaceable(psv_get_block(window, ax, ay, az))) {
                    int place_id = held.item;
                    /* World.mayPlace entity-collision gate: a block with a
                     * collision box cannot be placed intersecting the player
                     * bb (strict AABB intersects, PRE-move pose; oracle mobs
                     * are not simulated 1:1 so only the player is checked).
                     * This is what makes the t611 mid-jump nerd-pole place
                     * fail while t627 succeeds. */
                    if (psv_solid(place_id)) {
                        const McAABB *pb = &pl->ent.box;
                        if (pb->minX < (double)ax + 1.0 && pb->maxX > (double)ax &&
                            pb->minY < (double)ay + 1.0 && pb->maxY > (double)ay &&
                            pb->minZ < (double)az + 1.0 && pb->maxZ > (double)az)
                            place_id = 0;
                    }
                    /* only place if it looks like a block id (1..255); tools stay tools */
                    if (place_id > 0 && place_id < 256 && !ita_is_pickaxe(place_id)) {
                        int face = face_from_adj(hx, hy, hz, ax, ay, az);
                        int yq = yaw_to_quad(pl->yaw);
                        int sneaked = act.sneak;
                        int pmeta = place_id == IBP_BLK_TORCH
                            ? torch_placement_meta(window, ax, ay, az, face)
                            : ibp_placed_meta(place_id, face, yq, sneaked, held.meta) & 15;
                        if (pmeta < 0) place_id = 0;
                        if (place_id) {
                            ICStack used = isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
                            if (isr_is_empty(&used)) goto use_done;
                            psv_set_state(window, ax, ay, az, place_id, pmeta);
                            pl->place_events++;
                            emit_edit(edits, &ne, max_edits, ox, oy, oz, ax, ay, az,
                                      place_id, pmeta, 0, 0, 0);
                        }
                    }
                }
            }
        }
    }
use_done:

    /* Entity.handleWaterMovement: being in water zeroes fallDistance at the
     * START of the tick (before the move), so a landing on the same tick only
     * sees that tick's descent and swimming never banks fall damage. The tape
     * fall field shows the pattern (fall = single-tick drop while sinking).
     * Found at tape 20260712T055346Z t1288: the oracle lands on the pond floor
     * at hp 20 while magma had accumulated the whole ~13-block drop into the
     * water and took 10 fall damage, arming a spurious hurt velocity reset
     * (vz zeroed at t1289). */
    int water_pre = psv_in_liquid(window, &pl->ent, 1);
    int lava_pre  = water_pre ? 0 : psv_in_liquid(window, &pl->ent, 0);
    if (water_pre) pl->fall_distance = 0.0f;
    /* actual land jump this tick (psv branch order: liquid swim-up first) */
    int land_jump = act.jump && !was_air && !water_pre && !lava_pre;
    if (land_jump && act.sprint) {
        float fj = pl->yaw * 0.017453292f;
        s_server_motion_x -= (double)(mc_sin(st, fj) * 0.2f);
        s_server_motion_z += (double)(mc_cos(st, fj) * 0.2f);
    }

    /* EntityPlayerSP.onLivingUpdate gates START_FALL_FLYING on the pre-travel
     * pose (fresh jump, airborne, descending). Checking after travel would
     * wrongly arm flight on the apex tick where gravity flips motionY
     * negative mid-tick (MC-111444 must stay broken the vanilla way). The
     * integrated-server flag is consumed on the next tick's travel, matching
     * the elytra_dip tape (jump t55, first elytra travel t56).
     * NetHandlerPlayServer: !onGround && motionY < 0 && !flying && !inWater
     * plus a usable chest elytra. capabilities.isFlying is creative flight,
     * not gamemode.
     *
     * Flight eligibility: when chest (isr 38) holds Items.ELYTRA, derive the
     * equipped flag from ItemElytra.isBroken (usable). Other chest items clear
     * it. Empty chest preserves set_elytra (replay/test hook). */
    {
        ICStack chest = isr_get_stack(&pl->inv, ISR_ARMOR_CHEST);
        if (chest.item == ISR_ELYTRA_ITEM)
            pl->elytra_equipped = isr_elytra_usable(&chest);
        else if (!isr_is_empty(&chest))
            pl->elytra_equipped = 0;
    }
    int elytra_press = act.jump && !pl->prev_jump;
    int elytra_was = pl->elytra_flying;
    int elytra_can_start = !pl->ent.onGround && pl->ent.motionY < 0.0;
    psv_physics_tick(window, st, pl, &a, blocks);

    if (elytra_press && !elytra_was && pl->elytra_equipped && !water_pre &&
        elytra_can_start)
        pl->elytra_flying = 1;
    pl->prev_jump = act.jump;
    /* EntityLivingBase.onUpdate increments ticksElytraFlying only when the
     * flag was true for this onUpdate. Activation is post-travel, so the
     * arming tick must not count; the first travel tick still starts at 0
     * so updateElytra's (ticks+1)%20 damage cadence stays aligned. */
    if (elytra_was && pl->elytra_flying) {
        ++pl->ticks_elytra_flying;
        /* EntityLivingBase.updateElytra: damage chest elytra every 20 flying
         * ticks when the piece is still usable. */
        if ((pl->ticks_elytra_flying % 20) == 0) {
            ICStack chest = isr_get_stack(&pl->inv, ISR_ARMOR_CHEST);
            if (chest.item == ISR_ELYTRA_ITEM && chest.count > 0) {
                ITAStack e = ita_mk(chest.item, chest.meta);
                if (ita_attempt_damage(&e, 1, NULL)) {
                    isr_set_stack(&pl->inv, ISR_ARMOR_CHEST, ic_empty());
                    pl->elytra_equipped = 0;
                    pl->elytra_flying = 0;
                } else {
                    chest.meta = e.damage;
                    isr_set_stack(&pl->inv, ISR_ARMOR_CHEST, chest);
                    pl->elytra_equipped = isr_elytra_usable(&chest);
                    if (!pl->elytra_equipped) pl->elytra_flying = 0;
                }
            }
        }
    } else if (!pl->elytra_flying) {
        pl->ticks_elytra_flying = 0;
    }
    psv_update_elytra_size(window, pl, blocks);

    {
        ICStack food=isr_get_stack(&pl->inv,pl->inv.current_item);
        int hunger=0;float sat=0.0f;
        switch(food.item){
        case 260:hunger=4;sat=0.3f;break;case 297:hunger=5;sat=0.6f;break;
        case 319:case 363:hunger=3;sat=0.3f;break;case 320:case 364:hunger=8;sat=0.8f;break;
        case 365:case 423:hunger=2;sat=0.3f;break;case 366:case 424:hunger=6;sat=0.6f;break;
        default:break;
        }
        if(act.use&&hunger&&vit->foodLevel<20){
            if(s_eat_item!=food.item){s_eat_item=food.item;s_eat_ticks=0;}
            if(++s_eat_ticks>=32){
                (void)isr_decr_stack_size(&pl->inv,pl->inv.current_item,1);
                vit->foodLevel+=hunger;if(vit->foodLevel>20)vit->foodLevel=20;
                vit->saturation+=(float)hunger*sat*2.0f;
                if(vit->saturation>(float)vit->foodLevel)vit->saturation=(float)vit->foodLevel;
                pl->food=(float)vit->foodLevel;s_eat_ticks=0;s_eat_item=0;
                s_use_action=0;s_use_remaining=0;s_use_max=0;
            }else{
                /* getItemInUseCount counts down from max (32); elapsed = s_eat_ticks. */
                s_use_action=1;s_use_max=32;s_use_remaining=32-s_eat_ticks;
                if(s_use_remaining<0)s_use_remaining=0;
            }
        }else if(act.use&&(food.item==373||food.item==335)){
            /* Potion / milk: EnumAction.DRINK, same 32-tick transform as EAT. */
            if(s_eat_item!=food.item){s_eat_item=food.item;s_eat_ticks=0;}
            if(++s_eat_ticks>=32){
                (void)isr_decr_stack_size(&pl->inv,pl->inv.current_item,1);
                s_eat_ticks=0;s_eat_item=0;
                s_use_action=0;s_use_remaining=0;s_use_max=0;
            }else{
                s_use_action=1;s_use_max=32;s_use_remaining=32-s_eat_ticks;
                if(s_use_remaining<0)s_use_remaining=0;
            }
        }else if(act.use&&food.item==442){
            /* MC 1.11.2: only ItemShield has EnumAction.BLOCK (item 442).
             * Swords are EnumAction.NONE (combat update); getMaxItemUseDuration 72000. */
            s_use_action=2;s_use_max=72000;
            if(s_use_remaining<=0||s_use_remaining>72000)s_use_remaining=72000;
            if(s_use_remaining>0)--s_use_remaining;
            s_eat_ticks=0;s_eat_item=0;
        }else{
            s_eat_ticks=0;s_eat_item=0;
            if(food.item!=261){s_use_action=0;s_use_remaining=0;s_use_max=0;}
        }
    }

    if (a.attack) pl->swing_events++;

    {
        double dx = pl->ent.posX - pre_x, dy = pl->ent.posY - pre_y,
               dz = pl->ent.posZ - pre_z;
        float hp_before = vit->health;
        gm_vitals_apply(vit, pl, act, was_air, prev_min_y, dx, dy, dz,
                        water_pre, eye_in_water(window, pl), land_jump);
        if (vit->health < hp_before) {
            /* EntityTracker sends velocityChanged before the server's next
             * travel drag, so preserve this tick's shadow packet value. */
            s_hurt_vel_reset = 1;
        } else {
            float drag = 0.91f;
            if (pl->ent.onGround) {
                int bx = mc_floor(pl->ent.posX), by = mc_floor(pl->ent.posY) - 1;
                int bz = mc_floor(pl->ent.posZ);
                drag *= psv_slipperiness(psv_get_block(window, bx, by, bz));
            }
            s_server_motion_x *= (double)drag;
            s_server_motion_z *= (double)drag;
            if (fabs(s_server_motion_x) < 0.003) s_server_motion_x = 0.0;
            if (fabs(s_server_motion_z) < 0.003) s_server_motion_z = 0.0;
        }
    }

    *nedits = ne;
}

/* Live inventory slotClick on the player's hotbar (slots 0..8) + cursor.
 * Drives the verified container_click.h path. */
void gm_player_inv_click(struct PsvPlayer *pl_, int slot_id, int button, int click_type)
{
    PsvPlayer *pl = (PsvPlayer *)pl_;
    CcInv inv;
    int i;
    for (i = 0; i < CC_SLOTS; ++i)
        inv.slots[i] = isr_get_stack(&pl->inv, i);
    inv.cursor = s_cursor;
    cc_slot_click(&inv, slot_id, button, click_type);
    for (i = 0; i < CC_SLOTS; ++i)
        isr_set_stack(&pl->inv, i, inv.slots[i]);
    s_cursor = inv.cursor;
}

ICStack gm_player_cursor(void) { return s_cursor; }
void gm_player_cursor_set(ICStack s) { s_cursor = s; }
void gm_player_dig_reset(void) {
    s_dig_progress = 0.0f;
    s_dig_hx = INT_MIN;
    s_eat_ticks=0;s_eat_item=0;
    s_use_action=0;s_use_remaining=0;s_use_max=0;
    s_hurt_vel_reset=0;s_server_motion_x=0.0;s_server_motion_z=0.0;
}

void gm_player_set_packet_velocity(struct PsvPlayer *opaque,
                                   double x, double y, double z) {
    PsvPlayer *pl = (PsvPlayer *)opaque;
    if (!pl) return;
    pl->ent.motionX=x;pl->ent.motionY=y;pl->ent.motionZ=z;
    s_server_motion_x=x;s_server_motion_z=z;
    s_hurt_vel_reset=0;
}

void gm_player_clear_inferred_hurt_velocity(void) {
    s_hurt_vel_reset=0;
}

void gm_player_ctl_dig_export(GmPlayerCtlSnap *out) {
    out->dig_progress   = s_dig_progress;
    out->dig_hx         = s_dig_hx;
    out->dig_hy         = s_dig_hy;
    out->dig_hz         = s_dig_hz;
    out->dig_hitting    = s_dig_hitting;
    out->dig_delay      = s_dig_delay;
    out->atk_prev       = s_atk_prev;
    out->rc_delay       = s_rc_delay;
    out->use_prev       = s_use_prev;
    out->hurt_vel_reset = s_hurt_vel_reset;
    out->server_motion_x = s_server_motion_x;
    out->server_motion_z = s_server_motion_z;
}

void gm_player_ctl_dig_import(const GmPlayerCtlSnap *in) {
    s_dig_progress   = in->dig_progress;
    s_dig_hx         = in->dig_hx;
    s_dig_hy         = in->dig_hy;
    s_dig_hz         = in->dig_hz;
    s_dig_hitting    = in->dig_hitting;
    s_dig_delay      = in->dig_delay;
    s_atk_prev       = in->atk_prev;
    s_rc_delay       = in->rc_delay;
    s_use_prev       = in->use_prev;
    s_hurt_vel_reset = in->hurt_vel_reset;
    s_server_motion_x = in->server_motion_x;
    s_server_motion_z = in->server_motion_z;
}

/* Current progressive-dig target + damage 0..1 (RenderGlobal drawBlockDamageTexture
 * feed). Coords are window-LOCAL (caller adds ox/oz). Returns 0 when not digging. */
int gm_player_dig_state(int *lx, int *ly, int *lz, float *progress) {
    if (s_dig_hx == INT_MIN || s_dig_progress <= 0.0f)
        return 0;
    *lx = s_dig_hx; *ly = s_dig_hy; *lz = s_dig_hz;
    *progress = s_dig_progress > 1.0f ? 1.0f : s_dig_progress;
    return 1;
}

void gm_player_view(const struct PsvPlayer *pl_, int ox, int oz, GmPlayerView *out)
{
    const PsvPlayer *pl = (const PsvPlayer *)pl_;
    out->x = (float)(pl->ent.posX + (double)ox);
    out->y = (float)(pl->ent.posY);
    out->z = (float)(pl->ent.posZ + (double)oz);
    out->eye_height = (float)psv_player_eye_height(pl);

    out->yaw   = pl->yaw;
    out->pitch = pl->pitch;
    out->on_ground = pl->ent.onGround;

    out->health     = pl->health;
    out->max_health = PSV_MAX_HEALTH;
    out->food       = pl->food;
    out->max_food   = PSV_MAX_FOOD;

    out->xp_level = 0;
    out->xp_frac  = 0.0f;
    out->air      = -1;

    out->dead   = 0;
    out->deaths = 0;
    out->portal = 0.0f;
    out->portal_frame = -1;
    out->portal_phase = 0;
    out->loading = 0;
    out->fov_mult = s_fov_hand;
    out->bow_pull = s_bow_ticks;
    out->fire = 0;
    out->creative = 0;
    out->hurt_time = 0;
    out->hud_health = out->hud_last_health = 0;
    out->hud_flash = out->hud_state_valid = 0;
    out->hud_transition_lead = 0;
    out->use_action = s_use_action;
    out->use_remaining = s_use_remaining;
    out->use_max = s_use_max;
    /* Absorption: EntityLivingBase.getAbsorptionAmount. PvStats / PsvPlayer have
     * no absorption field (player_vitals documents it out of scope). Leave 0 —
     * do not invent gold hearts; HUD layout tests may still set pv.absorption. */
    out->absorption = 0.0f;

    /* ForgeHooks.getTotalArmorValue via ita_armor_set_points on equipped slots. */
    {
        ITAStack slots[4];
        for (int i = 0; i < 4; ++i) {
            ICStack s = isr_get_stack(&pl->inv, ISR_ARMOR0 + i);
            slots[i] = ita_mk(s.item, s.meta);
            slots[i].count = s.count;
        }
        int pts = ita_armor_set_points(slots);
        if (pts < 0) pts = 0;
        if (pts > 20) pts = 20;
        out->armor_points = pts;
    }

    int sel = pl->inv.current_item;
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    out->hotbar_sel = sel;

    for (int i = 0; i < 9; ++i) {
        ICStack s = isr_get_stack(&pl->inv, i);
        out->hotbar_ids[i]    = s.item;
        out->hotbar_counts[i] = s.count;
        out->hotbar_meta[i]   = s.meta;
    }
}
