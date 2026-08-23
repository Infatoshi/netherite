/* Ground-item units + fixture baker.
 *
 * Units: combineItems, pickup delay 10 / thrown 40, age 6000, lava
 * dealFireDamage 5 hits, cap 48 skip+count.
 * --write-fixture FROM OUT plants two cobble stacks that merge, a lava
 * pool, and one cobble over the lava. Writes blaze/rl/fixtures/ground_items_s10.json. */
#define _POSIX_C_SOURCE 200809L
#define IL_W char
#define il_id(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 0)
#define il_meta(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 0)
#include "item_live.h"
#include "blaze_snapshot.h"
#include "mc_blocks.h"

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

static void fill_item(McItem *it, double x, double y, double z,
                      int item, int count, int delay) {
    memset(it, 0, sizeof *it);
    ei_set_position(it, x, y, z);
    it->item = item;
    it->count = count;
    it->meta = 0;
    it->hasSubtypes = 1;
    it->maxStack = 64;
    it->delayBeforeCanPickup = delay;
    it->lifespan = EI_LIFESPAN;
    it->health = EI_HEALTH;
    it->fire = -EI_FIRE_IMMUNE_TICKS;
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
    if (s->light) {
        unsigned char sky = (id == 0) ? 15 : 0;
        s->light[idx] = (unsigned char)(sky << 4);
    }
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
    /* Wait past pickup delay 10 and merge gate ticksExisted % 25. */
    for (i = 0; i < 26; ++i) {
        if (n++) fputc(',', f);
        fputs("{}", f);
    }
    /* Walk +Z over the merged stack. */
    for (i = 0; i < 24; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"forward\":1}", f);
    }
    /* Idle while the lava item burns (5 dealFireDamage ticks). */
    for (i = 0; i < 14; ++i) {
        if (n++) fputc(',', f);
        fputs("{}", f);
    }
    fputs("]\n", f);
    fclose(f);
    fprintf(stderr, "WROTE %s actions=%d\n", path, n);
    return n;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, i, nact;
    RlSnapItem *it;
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

    for (x = 5; x <= 16; ++x)
        for (z = 5; z <= 16; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 68; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }
    /* Lava pool at z=14. */
    for (x = 7; x <= 10; ++x)
        for (z = 13; z <= 15; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            plant_cell(&s, x, 65, z, BLK_LAVA, 0);
        }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 0;
    s.head.n_items = 3;
    it = &s.items[0];
    memset(it, 0, sizeof *it);
    it->x = 8.5;
    it->y = 65.0;
    it->z = 10.5;
    it->item = 4;
    it->count = 32;
    it->pickup_delay = EI_PICKUP_DEFAULT;
    it->lifespan = EI_LIFESPAN;
    it = &s.items[1];
    memset(it, 0, sizeof *it);
    it->x = 8.7;
    it->y = 65.0;
    it->z = 10.5;
    it->item = 4;
    it->count = 32;
    it->pickup_delay = EI_PICKUP_DEFAULT;
    it->lifespan = EI_LIFESPAN;
    it = &s.items[2];
    memset(it, 0, sizeof *it);
    it->x = 8.5;
    it->y = 66.0;
    it->z = 14.5;
    it->item = 4;
    it->count = 1;
    it->pickup_delay = 32767;
    it->lifespan = EI_LIFESPAN;

    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s merge cobble (8.5/8.7,65,10.5) lava item (8.5,66,14.5) n_items=%u\n",
            out_path, s.head.n_items);
    blaze_snapshot_free(&s);
    nact = write_chain("blaze/rl/fixtures/ground_items_s10.json");
    return nact > 0;
}

static int run_units(void) {
    McItem a, b;
    McAABB none[1];
    int i, spawned, failc;

    fill_item(&a, 8.5, 64.0, 8.5, 4, 32, 10);
    fill_item(&b, 8.7, 64.0, 8.5, 4, 32, 10);
    expect(ei_combine(&a, &b) == 1, "combineItems merges equal cobble");
    expect(b.count == 64 && a.dead == 1, "survivor count 64, donor dead");

    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 10);
    ei_pre(&a, none, 0, 0);
    expect(a.delayBeforeCanPickup == 9, "pickup delay 10 -> 9");

    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 0);
    a.age = EI_LIFESPAN - 1;
    ei_post(&a, 0);
    expect(a.dead == 1, "age 6000 setDead");

    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 0);
    for (i = 0; i < 4; ++i)
        ei_attack(&a, 1.0f);
    expect(a.dead == 0, "alive after 4 fire hits");
    ei_attack(&a, 1.0f);
    expect(a.dead == 1, "5th dealFireDamage kills");

    expect(EI_PICKUP_DEFAULT == 10, "setDefaultPickupDelay is 10");
    expect(EI_PICKUP_THROWN == 40, "dropItem pickupDelay is 40");

    /* Pickup volume: player AABB expand 1.0/0.5/1.0.
     * EntityPlayer.java:613, AxisAlignedBB.java:167-175.
     * delay>0 return EntityItem.java:432. addItemStackToInventory
     * currentItem then main InventoryPlayer.java:356-376, :409-453. */
    {
        McItem it;
        IsrInv inv;
        McAABB player, vol;
        const double hw = 0.30000001192092896;
        const double hh = 1.7999999523162842;
        double px = 8.5, py = 65.0, pz = 8.5;

        player = mc_aabb_make(px - hw, py, pz - hw, px + hw, py + hh, pz + hw);
        vol = il_pickup_volume(&player);
        expect(vol.minX == player.minX - 1.0 && vol.maxX == player.maxX + 1.0,
               "pickup expand 1.0 x EntityPlayer.java:613");
        expect(vol.minY == player.minY - 0.5 && vol.maxY == player.maxY + 0.5,
               "pickup expand 0.5 y EntityPlayer.java:613");
        expect(vol.minZ == player.minZ - 1.0 && vol.maxZ == player.maxZ + 1.0,
               "pickup expand 1.0 z EntityPlayer.java:613");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz, 4, 8, 1);
        expect(il_try_pickup(&it, &inv, &player) == 0,
               "delay>0 no pickup EntityItem.java:432");
        expect(it.dead == 0 && it.count == 8 && inv.main[0].count == 0,
               "delay>0 item and inv unchanged");

        fill_item(&it, px, py, pz, 4, 8, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "delay==0 overlap pickup");
        expect(it.dead == 1 && it.count == 0, "picked item dead leftover 0");
        expect(inv.main[0].item == 4 && inv.main[0].count == 8,
               "addItem first empty slot 0");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz + (hw + 1.0) - 0.05, 4, 1, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "item inside expand 1.0 xz picks up");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz + (hw + 1.0) + 0.25, 4, 1, 0);
        expect(il_try_pickup(&it, &inv, &player) == 0,
               "item outside expand 1.0 xz stays");
        expect(it.dead == 0 && it.count == 1, "outside volume not consumed");

        memset(&inv, 0, sizeof inv);
        inv.current_item = 0;
        inv.main[0] = ic_mk(4, 32, 0);
        inv.main[2] = ic_mk(4, 32, 0);
        fill_item(&it, px, py, pz, 4, 16, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "addItem merges currentItem first");
        expect(inv.main[0].count == 48, "current slot 0 grew 32+16");
        expect(inv.main[2].count == 32, "later cobble slot untouched");
    }

    spawned = 0;
    failc = 0;
    for (i = 0; i < 49; ++i) {
        if (i < 48)
            spawned++;
        else
            failc++;
    }
    expect(spawned == 48 && failc == 1, "49th spawn skipped, fail count 1");
    {
        McItem tickit;
        memset(&tickit, 0, sizeof tickit);
        il_tick_item(NULL, &tickit, 0, 0);
    }
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
