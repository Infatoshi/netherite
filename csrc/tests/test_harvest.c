/* Gate: the native port of block breaking speed and harvestability against the
 * oracle's harvest probe (oracle/harness/netherite/oracle/HarvestProbe.java, recorded
 * with `make run CLASS=HarvestProbe`).
 *
 * Every case in cases.bin carries one (block, meta) pair, one held item with an
 * Efficiency level, Haste and Mining Fatigue (0 = not active, else the amplifier
 * plus 1) and onGround. The probe put those on the server player and recorded
 * EntityPlayer.canHarvestBlock, getCurrentPlayerStrVsBlock(block, false) and
 * Block.getPlayerRelativeBlockHardness, plus the block's own hardness.
 *
 * This replays every record through harvest.c and compares the three answers as
 * raw float bits, the boolean, and the hardness, and checks the hardness the file
 * carries for a block against BLOCKS[id].hardness so a generator that misreads a
 * hardness fails here. The registries themselves (material, tool material,
 * efficiency, tool_not_required) are pinned indirectly: a wrong entry changes one
 * of the three answers for some case.
 *
 * Every record also has to agree with itself: held_item/held_lvl are what
 * getHeldItem() returned, which is what getEfficiencyModifier and func_146023_a
 * read, and resp_item/resp_lvl is inventory.mainInventory[currentItem], which is
 * what func_146025_b reads. The probe sets both to the same stack; a record where
 * they differ would be testing something else, so it is a failure here too.
 *
 * Cases the probe marked skipped (material water in the way, or above y 255)
 * carry zeros and are counted, not compared; the manifest says what they were.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/harvest.h"

#define REC_BYTES 61

static long bad;

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t i32(const unsigned char *p)
{
    return (int32_t)le32(p);
}

static int16_t i16(const unsigned char *p)
{
    return (int16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static float f32(const unsigned char *p)
{
    uint32_t u = le32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* Bit for bit, and the two NaN shapes the game produces count as equal. */
static int same_float(float a, float b)
{
    return memcmp(&a, &b, 4) == 0 || (a != a && b != b);
}

static unsigned char *read_whole(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { fprintf(stderr, "short read %s\n", path); exit(2); }
    fclose(f);
    *n = (size_t)len;
    return buf;
}

struct case_rec {
    int32_t x, y, z;
    int dim, block_ip, meta;
    float hardness;
    int state, haste, fatigue;
    int held_item, held_size, held_ench, held_lvl;
    int resp_item, resp_size, resp_ench, resp_lvl;
    int item[4];
    int can_harvest;
    float speed, rel;
    int flags;
};

static void parse(const unsigned char *p, struct case_rec *c)
{
    c->x = i32(p + 4);
    c->y = i32(p + 8);
    c->z = i32(p + 12);
    c->dim = p[16];
    c->block_ip = i32(p + 17);
    c->meta = (signed char)p[21];
    c->hardness = f32(p + 22);
    c->state = p[26];
    c->haste = p[27];
    c->fatigue = p[28];
    c->held_item = i16(p + 29);
    c->held_size = (signed char)p[31];
    c->held_ench = i16(p + 32);
    c->held_lvl = i16(p + 34);
    c->resp_item = i16(p + 36);
    c->resp_size = (signed char)p[38];
    c->resp_ench = i16(p + 39);
    c->resp_lvl = i16(p + 41);
    for (int i = 0; i < 4; ++i) c->item[i] = i16(p + 43 + 2 * i);
    c->can_harvest = p[51];
    c->speed = f32(p + 52);
    c->rel = f32(p + 56);
    c->flags = p[60];
}

/* The hardness the file carries for each block id, from its first case: the
 * probe's own Block.getBlockHardness. */
static float file_hardness[4096];
static int file_hardness_set[4096];

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1100];
    snprintf(path, sizeof path, "%s/cases.bin", dir);
    size_t n;
    unsigned char *d = read_whole(path, &n);
    if (n < 4) { fprintf(stderr, "%s: too short\n", path); return 2; }
    int32_t count = i32(d);
    if (n != 4 + (size_t)count * REC_BYTES)
    {
        printf("FAIL %s: %d cases want %zu bytes, file is %zu\n", path, count, 4 + (size_t)count * REC_BYTES, n);
        return 1;
    }

    long checked = 0, skipped = 0, water = 0;
    int blocks_seen = 0;

    for (int32_t i = 0; i < count; ++i)
    {
        struct case_rec c;
        parse(d + 4 + (size_t)i * REC_BYTES, &c);

        if (c.state != 0)
        {
            ++skipped;
            if (c.flags & 1) ++water;
            continue;
        }
        if (c.block_ip < 0 || c.block_ip >= (int)(sizeof BLOCKS / sizeof BLOCKS[0]))
        {
            printf("FAIL case %d: block id %d out of range\n", i, c.block_ip);
            ++bad;
            continue;
        }

        if (!file_hardness_set[c.block_ip])
        {
            file_hardness_set[c.block_ip] = 1;
            file_hardness[c.block_ip] = c.hardness;
            ++blocks_seen;
        }
        else if (!same_float(file_hardness[c.block_ip], c.hardness))
        {
            printf("FAIL case %d: block %d hardness %a then %a in the same file\n",
                   i, c.block_ip, file_hardness[c.block_ip], c.hardness);
            ++bad;
        }

        if (c.held_item != c.resp_item || c.held_lvl != c.resp_lvl || c.held_size != c.resp_size)
        {
            printf("FAIL case %d: held(%d lvl %d size %d) != resp(%d lvl %d size %d)\n",
                   i, c.held_item, c.held_lvl, c.held_size, c.resp_item, c.resp_lvl, c.resp_size);
            ++bad;
        }
        if (c.item[0] != c.held_item)
        {
            printf("FAIL case %d: item0 %d != held %d\n", i, c.item[0], c.held_item);
            ++bad;
        }

        struct harvest_player p = {
            .held_item = c.held_item,
            .held_enchant = c.held_lvl,
            .haste = c.haste,
            .fatigue = c.fatigue,
            .on_ground = (c.flags & 8) ? 1 : 0,
        };

        int got_harvest = can_harvest_block(&p, c.block_ip);
        float got_hard = block_hardness(c.block_ip);
        float got_speed = player_str_vs_block(&p, c.block_ip);
        float got_rel = player_relative_block_hardness(&p, c.block_ip);

        ++checked;
        if (got_harvest != c.can_harvest)
        {
            printf("FAIL case %d block %d (%s) item %d: canHarvest want %d got %d\n",
                   i, c.block_ip, BLOCKS[c.block_ip].name, c.held_item, c.can_harvest, got_harvest);
            ++bad;
        }
        if (!same_float(got_hard, c.hardness))
        {
            printf("FAIL case %d block %d (%s): hardness want %a got %a\n",
                   i, c.block_ip, BLOCKS[c.block_ip].name, c.hardness, got_hard);
            ++bad;
        }
        if (!same_float(got_speed, c.speed))
        {
            printf("FAIL case %d block %d (%s) item %d eff %d haste %d fat %d ground %d: speed want %a got %a\n",
                   i, c.block_ip, BLOCKS[c.block_ip].name, c.held_item, c.held_lvl, c.haste, c.fatigue,
                   p.on_ground, c.speed, got_speed);
            ++bad;
        }
        if (!same_float(got_rel, c.rel))
        {
            printf("FAIL case %d block %d (%s) item %d eff %d: relHardness want %a got %a\n",
                   i, c.block_ip, BLOCKS[c.block_ip].name, c.held_item, c.held_lvl, c.rel, got_rel);
            ++bad;
        }
        if (bad > 20) { printf("... stopping after %ld failures\n", bad); break; }
    }

    free(d);

    if (bad == 0)
        printf("PASS harvest %s: %ld cases, %ld skipped (%ld water blocks), %d blocks\n",
               dir, checked, skipped, water, blocks_seen);
    return bad != 0;
}