/* Potion / milk / shield units + fixture baker.
 *
 * Units cite Potion.java isReady/performEffect, ItemPotion duration 32,
 * ItemBucketMilk cure, EntityPlayer.damageShield.
 * --write-fixture FROM OUT plants a regen potion in the hotbar, a shield
 * in the offhand, a witch, and a zombie, then writes potions_s10.json. */
#define _POSIX_C_SOURCE 200809L
#include "player_survival.h"
#include "blaze_snapshot.h"
#include "hostile_live.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

static void plant_cell(CuSnapshot *s, int wx, int wy, int wz, int id, int meta) {
    int lx = wx - s->head.rx0;
    int ly = wy - s->head.ry0;
    int lz = wz - s->head.rz0;
    long idx;
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= s->head.rnx || ly >= s->head.rny || lz >= s->head.rnz)
        return;
    idx = ((long)lx * s->head.rny + ly) * s->head.rnz + lz;
    s->cells[idx] = (unsigned short)(((id & 4095) << 4) | (meta & 15));
    if (s->light)
        s->light[idx] = (unsigned char)(15 << 4);
}

static void plant_hostile(RlSnapMob *o, int slot, int id, int type,
                          double x, double y, double z) {
    float w, h;
    ehs_size((u8)type, &w, &h);
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = type;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = ehs_max_health_of((u8)type, 0);
    o->on_ground = 1;
    o->box_on = 1;
    o->box_minx = x - (double)(w * 0.5f);
    o->box_miny = y;
    o->box_minz = z - (double)(w * 0.5f);
    o->box_maxx = x + (double)(w * 0.5f);
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)(w * 0.5f);
    o->seed48 = 1;
}

static int write_chain(const char *path) {
    FILE *f;
    int i, n = 0;
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    fputc('[', f);
    /* Drink the planted regen potion (ItemPotion.java:90 duration 32). */
    for (i = 0; i < 32; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"use\":1}", f);
    }
    /* Idle: regen ticks + witch splash (poison). */
    for (i = 0; i < 48; ++i) {
        if (n++) fputc(',', f);
        fputs("{}", f);
    }
    /* Offhand shield: use after the bottle is in main. */
    for (i = 0; i < 40; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"use\":1}", f);
    }
    fputs("]\n", f);
    fclose(f);
    fprintf(stderr, "WROTE %s actions=%d\n", path, n);
    return n;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, i;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    s.head.py = 65.0;
    s.head.box[1] = 65.0;
    s.head.box[4] = 65.0 + 1.8;
    s.head.on_ground = 1;
    s.head.mx = s.head.my = s.head.mz = 0.0;
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
    s.head.px = 8.5;
    s.head.pz = 8.5;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 8.5 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 8.5 + 0.3;
    for (i = 0; i < 37; ++i)
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;
    /* Plant regen potion in hotbar 0 (PotionType.java:81 duration 900). */
    s.head.inv[0][0] = PSV_ITEM_POTION;
    s.head.inv[0][1] = 1;
    s.head.inv[0][2] = PSV_PTYPE_REGENERATION;
    s.head.inv[1][0] = PSV_ITEM_MILK;
    s.head.inv[1][1] = 1;
    /* Offhand shield ItemShield.java:27. Snapshot inv[36] is offhand. */
    s.head.inv[36][0] = PSV_ITEM_SHIELD;
    s.head.inv[36][1] = 1;
    s.head.inv[36][2] = 0;
    s.head.hotbar_sel = 0;

    for (x = 5; x <= 14; ++x)
        for (z = 5; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 67; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 2;
    plant_hostile(&s.mobs[0], 1, 1, EW_TYPE_ZOMBIE, 8.5, 65.0, 10.5);
    plant_hostile(&s.mobs[1], 2, 2, EW_TYPE_WITCH, 8.5, 65.0, 11.5);
    s.player_fire = 0;
    s.player_air = PSV_AIR_MAX;
    s.n_potions = 0;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s potions hotbar0=%d meta=%d offhand=%d witch+zombie\n",
            out_path, s.head.inv[0][0], s.head.inv[0][2], s.head.inv[36][0]);
    blaze_snapshot_free(&s);
    return write_chain("blaze/rl/fixtures/potions_s10.json") > 0;
}

static int run_units(void) {
    PsvPlayer pl;
    PvStats vit;
    int t, heals;

    psv_player_init(&pl);
    pv_init(&vit);
    vit.health = 10.0f;
    pl.health = 10.0f;

    expect(psv_potion_is_ready(PSV_POT_REGENERATION, 50, 0) == 1,
           "regen I isReady at duration 50 (50 >> 0)");
    expect(psv_potion_is_ready(PSV_POT_REGENERATION, 49, 0) == 0,
           "regen I not ready at 49");
    expect(psv_potion_is_ready(PSV_POT_REGENERATION, 25, 1) == 1,
           "regen II isReady at 25 (50 >> 1)");
    expect(psv_potion_is_ready(PSV_POT_POISON, 25, 0) == 1,
           "poison I isReady at 25");
    expect(psv_potion_is_ready(PSV_POT_WITHER, 40, 0) == 1,
           "wither I isReady at 40");
    expect(psv_potion_is_ready(PSV_POT_HUNGER, 7, 0) == 1,
           "hunger isReady every tick");

    psv_potion_add(&pl, PSV_POT_REGENERATION, 50, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.health == 11.0f, "regen I heals 1.0 on duration 50");
    expect(pl.n_potions == 1 && pl.potions[0].duration == 49,
           "regen duration decrements after perform");

    heals = 0;
    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.health = 1.0f;
    pl.health = 1.0f;
    psv_potion_add(&pl, PSV_POT_REGENERATION, 900, 0, 0, 1);
    for (t = 0; t < 900; ++t) {
        float before = vit.health;
        psv_update_potion_effects(&pl, &vit);
        if (vit.health > before) ++heals;
    }
    expect(heals == 18, "regen I 900 ticks heals 18 times (900,850,..50)");

    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.health = 1.0f;
    pl.health = 1.0f;
    psv_potion_add(&pl, PSV_POT_POISON, 25, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.health == 1.0f, "poison does not kill below 1.0");

    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.health = 1.0f;
    pl.health = 1.0f;
    psv_potion_add(&pl, PSV_POT_WITHER, 40, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.health == 0.0f, "wither kills at 1.0");

    psv_potion_clear(&pl);
    pv_init(&vit);
    psv_potion_add(&pl, PSV_POT_HUNGER, 10, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.exhaustion == 0.005f, "hunger I addExhaustion 0.005");
    psv_potion_clear(&pl);
    pv_init(&vit);
    psv_potion_add(&pl, PSV_POT_HUNGER, 10, 1, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.exhaustion == 0.010f, "hunger II addExhaustion 0.010");

    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.foodLevel = 10;
    vit.saturation = 0.0f;
    psv_potion_perform(&pl, &vit, PSV_POT_SATURATION, 0);
    expect(vit.foodLevel == 11 && vit.saturation == 2.0f,
           "saturation I addStats(1, 1.0)");

    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_SPEED, 100, 0, 0, 1);
    psv_potion_add(&pl, PSV_POT_POISON, 50, 0, 0, 1);
    psv_potion_milk_finish(&pl, 0, 1);
    expect(pl.n_potions == 0, "milk clears all effects");

    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_SPEED, 100, 0, 0, 1);
    psv_potion_add(&pl, PSV_POT_SPEED, 40, 1, 0, 1);
    expect(pl.n_potions == 1 && pl.potions[0].amplifier == 1 &&
               pl.potions[0].duration == 40,
           "combine: higher amplifier replaces duration");
    psv_potion_add(&pl, PSV_POT_SPEED, 80, 1, 0, 1);
    expect(pl.potions[0].duration == 80,
           "combine: same amplifier keeps longer duration");

    expect(psv_potion_move_mul(&pl) > 1.0f, "speed I multiplies move");
    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_JUMP_BOOST, 100, 0, 0, 1);
    expect(psv_jump_boost_extra(&pl) == 0.1f, "jump boost I +0.1 motionY");
    expect(psv_jump_boost_fall(&pl) == 1.0f, "jump boost I fall -1");
    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_STRENGTH, 100, 0, 0, 1);
    expect(psv_attack_damage_bonus(&pl) == 3.0f, "strength I +3 damage");
    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_WEAKNESS, 100, 0, 0, 1);
    expect(psv_attack_damage_bonus(&pl) == -4.0f, "weakness I -4 damage");
    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_RESISTANCE, 100, 0, 0, 1);
    expect(psv_resistance_scale(&pl, 0) == 0.8f, "resistance I *20/25");

    psv_potion_clear(&pl);
    pv_init(&vit);
    psv_potion_affect(&pl, &vit, PSV_POT_INSTANT_HEALTH, 0, 1.0);
    expect(vit.health == 20.0f, "instant health I heals 4 (clamped at 20)");
    vit.health = 10.0f;
    pl.health = 10.0f;
    psv_potion_affect(&pl, &vit, PSV_POT_INSTANT_DAMAGE, 0, 1.0);
    expect(vit.health == 4.0f, "instant damage I MAGIC 6");

    /* Drink: duration 32 finish applies regen and leaves a bottle. */
    psv_player_init(&pl);
    pv_init(&vit);
    isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_POTION, 1, PSV_PTYPE_REGENERATION));
    psv_potion_drink_finish(&pl, &vit, 0, 0);
    expect(psv_potion_is_active(&pl, PSV_POT_REGENERATION),
           "drink regen applies effect");
    expect(isr_get_stack(&pl.inv, 0).item == PSV_ITEM_GLASS_BOTTLE,
           "drink returns glass bottle");

    psv_potion_add(&pl, PSV_POT_POISON, 100, 0, 0, 1);
    isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_MILK, 1, 0));
    psv_potion_milk_finish(&pl, 0, 0);
    expect(pl.n_potions == 0, "milk finish clears");
    expect(isr_get_stack(&pl.inv, 0).item == PSV_ITEM_BUCKET,
           "milk returns bucket");

    /* Shield: raise 5 ticks, block a +Z source while looking +Z. */
    psv_player_init(&pl);
    isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_SHIELD, 1, 0));
    pl.yaw = 0.0f;
    pl.pitch = 0.0f;
    pl.use_action = PSV_USE_BLOCK;
    pl.use_max = PSV_SHIELD_USE_TICKS;
    pl.use_remaining = PSV_SHIELD_USE_TICKS - PSV_SHIELD_RAISE_TICKS;
    expect(psv_is_blocking(&pl) == 1, "shield blocking after 5 ticks");
    expect(psv_can_block_damage(&pl, PSV_HURT_PROJECTILE, pl.ent.posX,
                               pl.ent.posZ + 2.0) == 1,
           "shield blocks an arrow from +Z");
    expect(psv_can_block_damage(&pl, 0, pl.ent.posX, pl.ent.posZ + 2.0) == 1,
           "shield blocks a zombie from +Z");
    expect(psv_can_block_damage(&pl, PSV_HURT_BYPASS, pl.ent.posX,
                               pl.ent.posZ + 2.0) == 0,
           "unblockable is not blocked");
    {
        float dmg = 4.0f;
        int blocked = 0;
        psv_hurt_pre(&pl, &dmg, 0, pl.ent.posX, pl.ent.posZ + 2.0, &blocked);
        expect(blocked == 1 && dmg == 0.0f, "blocked hit zeros amount");
        expect(isr_get_stack(&pl.inv, 0).meta == 1 + 4,
               "damageShield 4.0 -> 1+floor(4) durability");
    }
    {
        float dmg = 2.0f;
        isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_SHIELD, 1, 0));
        psv_damage_shield(&pl, dmg);
        expect(isr_get_stack(&pl.inv, 0).meta == 0,
               "damageShield ignores damage < 3.0");
    }

    psv_potion_clear(&pl);
    psv_potion_apply_type(&pl, &vit, PSV_PTYPE_POISON, 1.0);
    expect(psv_potion_is_active(&pl, PSV_POT_POISON) &&
               pl.potions[0].duration == 900,
           "splash poison type applies 900 ticks");

    expect(PSV_POTION_MAX == 32, "cap 32 > 27 vanilla ids");
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    const char *from = NULL, *out = NULL;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            from = argv[++i];
            out = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--write-fixture FROM.bsnp OUT.bsnp]\n",
                    argv[0]);
            return 2;
        }
    }
    if (run_units())
        return 1;
    if (from && out && !write_fixture(from, out))
        return 1;
    return 0;
}
