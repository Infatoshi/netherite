/* chain (lane/chain): a dragon kill from a world's spawn by checkpointed search, on the CPUs, and the proof that the
 * found chain is one honest episode.
 *
 *   out/runtime/train/chain search --run FILE --dir DIR [--n N] [--groups G] [--threads T] [--seg S] [--free F]
 *                                  [--keep K] [--live L] [--minutes M] [--target RUNG] [--cam W] [--keys P] [--seed K]
 *                                  [--save-wait W] [--every SEC] [--mopt NAME=WORD]... [--resume DIR]
 *                                  [--fresh-memory 0|1] (0 refuses a missing or incompatible saved controller)
 *                                  [--water-seg SEC] (opt-in longer water-bucket legs and held diamond-kit progress)
 *                                  [--land-seg SEC] (opt-in longer on-land station and armor legs)
 *                                  [--snow-min 16|32|64] (held snowballs that count as the End kit; match snow_target)
 *                                  [--room-score-macro 0|1] (opt-in: snow-to-room progress uses the macro's room)
 *                                  [--post-stock-food P] (opt-in: after food_stock, the End floor is P and travel
 *                                                         keeps one distance stride across meals; 0 old behaviour)
 *                                  [--force-save-at N] (one-env diagnostic: a safe-exit seam without a rung gain)
 *                                  [--armor-min 13..15] (iron armor points before later rungs)
 *                                  [--late-health-min 0..20] [--late-food-min 0..1024] (floor to count snow or room)
 *                                  [--segment-log FILE] (one row per finished pearl segment for paired probes)
 *                                  [--death-log FILE] (each death's preceding air, fire, food, fall and mob clues)
 *                                  [--supply-log FILE] (with --rollout-out, books only: what is held and worn at the
 *                                                       first Nether step and at the end: supply_row)
 *                                  [--end-log FILE] (with --rollout-out: a row an attempt as it ends, the End books:
 *                                                    place, phase, the End's death's cause, push, phase and place)
 *                                  [--rollout-out FILE] (N independent one-shot attempts from the run's start,
 *                                                        or one --resume-ids ID replay shared by all N attempts;
 *                                                        each stops at --target or --seg seconds)
 *                                  [--chooser FILE] (with --rollout-out: each step's action drawn from a pick table
 *                                                    of proved kills' steps by rung and last kind, not random)
 *                                  [--start-latched 1] (with --rollout-out, no --resume: the run's start, a library
 *                                                       start made outside the chain, counts as past the meat,
 *                                                       provisions and carry latches, fed and armed)
 *                                  [--dev 1] (lane/rods: the run's start may be a dev start, its header's dev ops
 *                                            applied at tick 0: a what-if's rows, never a benchmark's)
 *                                  [--target-stage NAME[:BASE]] (with --rollout-out: an attempt reaches when stages.h's
 *                                                       stage NAME holds, read past the ladder's gates; BASE the
 *                                                       crystals a crystals_goneM start had; lane/legs)
 *                                  [--target-gain [KIND:]N] (with --rollout-out, not with --target-stage: an attempt
 *                                                       reaches at N more of KIND held than at its first step; KIND
 *                                                       pearl (ender pearls and eyes, lane/proom's; a bare N means
 *                                                       pearl) or rod (rods, powder and eyes as rods: chain's rod_eq;
 *                                                       lane/rods) or obs (obsidian held; lane/bportal))
 *                                  [--build-log FILE] (with --rollout-out, books only: a row an attempt, the portal
 *                                                       build's first steps: a new obsidian, 10 held, the obsidian
 *                                                       macro's want met, the frame plan begun, a whole frame and a
 *                                                       portal block in the client world round the feet, the Nether;
 *                                                       the bucket portal's and obsidian macro's books; lane/bportal)
 *                                  [--ladder-log FILE] (with --rollout-out, books only: a row an attempt, the first
 *                                                       step each End rung held, crystals_gone1 .. dragon_dead and the
 *                                                       exit, and the kit at the first End step and at the end;
 *                                                       lane/dleg)
 *                                  [--portal-trace I[/K]:FILE] (with --rollout-out: attempt I's, -1 every attempt's,
 *                                                       overworld steps, every K-th: the build's items, the bucket
 *                                                       portal's, obsidian macro's and frame plan's start tests and
 *                                                       state; vecenv.h neth_vec_bp_trace, read only; lane/bportal)
 *                                  [--hunt-log FILE] (with --rollout-out: a row an attempt, the pearl hunt: each new
 *                                                       pearl's step, day time and owner, the end's state and the
 *                                                       pearl macro's death and census books; lane/proom)
 *                                  [--gain-log FILE] (with --target-gain: a row an attempt, each gain's step, owner bit
 *                                                       and health, the end's state and the Nether macro's kill and
 *                                                       death books; lane/rods)
 *                                  [--night-log FILE] (with --rollout-out, read only: a row an attempt a night, at each
 *                                                       dawn and the end: health, food, pearls, the stats' endermen
 *                                                       killed, endermen seen and screaming in view, the census's
 *                                                       alive within 64 and 128, the pearl phases; lane/pnight)
 *                                  [--dig-log FILE] (with --rollout-out, read only, PRIVILEGED: a row an enderman's
 *                                                       visit within 48 blocks and a row an attempt, struct dg_acc;
 *                                                       lane/pdig; and a row a night: the ground covered; lane/pground:
 *                                                       the G row a night, the ground's biome, height and columns within
 *                                                       32 by kind, the distinct endermen with a clear line within 64;
 *                                                       the E row a night, every enderman within 128 by id: born or
 *                                                       arrived, how it left, its steps in range, a line in reach)
 *                                  [--sight-log FILE] (with --rollout-out, read only: a row an enderman seen in view,
 *                                                       its first distance, scream and end; lane/pdig)
 *                                  [--death-trace FILE] (with --rollout-out, read only: each dead attempt's last 400
 *                                                       steps, the pearl macro's phase, the ground, the hostiles near
 *                                                       by kind (the census's, PRIVILEGED), health, food and the
 *                                                       held item; lane/proom)
 *                                  [--lava-log FILE] (with --rollout-out, books only: each dead attempt's last 400
 *                                                       steps, the owner, the act, the crosshair, the macros' own lava
 *                                                       senses (neth_vec_lava: lava in view, lava_edge's and strip2's
 *                                                       tests, the escape) and PRIVILEGED the client world's lava
 *                                                       round the body; lane/lava)
 *                                  [--owner-census FILE] (with --rollout-out: each attempt's steps by owning macro, the
 *                                                       label's bit, and by kind; lane/legs)
 *                                  [--shold-trace I[/K]:FILE] (with --rollout-out: attempt I's, -1 every attempt's,
 *                                                       overworld steps, every K-th: the stronghold latch and each of
 *                                                       its holds, the oracle walk, provision_mode, the sky, the pearl
 *                                                       macro; vecenv.h neth_vec_sh_trace, read only; lane/proom)
 *                                  [--end-trace-hunt 1] (with --end-trace: each row adds the crystal hunt's state,
 *                                                       vecenv.h neth_vec_dragon_hunt, and the snowballs held; lane/legs)
 *                                  [--end-trace-ender 1] (with --end-trace, PRIVILEGED, read only: each row adds the
 *                                                       census's endermen: alive, listed, the nearest, the screaming
 *                                                       ones, those in the stare's cone with the line clear; lane/dleg)
 *                                  [--tape-attempt I:PATH] (with --rollout-out, no --resume: attempt I's episode written
 *                                                       as the oracle's script, neth_vec_tape)
 *                                  [--replay-script FILE:STEP] (with --rollout-out, no --resume: every attempt plays
 *                                                       an oracle script's acts from its STEP-th on, the known-answer
 *                                                       check of a library start harvested at that tick)
 *                                  [--stage-log FILE] (lane/fwd, with --rollout-out, books only: a row an attempt's
 *                                                       first hit of each event, fw_ev's: stages.h's chain from the
 *                                                       wooden pickaxe to the dragon's death (its crystals_goneN the
 *                                                       server's crystals by pillar, --ladder-log's lc_ngone, never the
 *                                                       macro's hit books), seven rods held (chain's rod_eq), twelve
 *                                                       pearls or eyes held, twelve eyes, all ten crystals gone; and an
 *                                                       END row an attempt: result, ticks, the state)
 *                                  [--wood-log FILE] (lane/wood, with --rollout-out, books only, overworld steps: an S
 *                                                       row an attempt at its first step (PRIVILEGED: the logs round
 *                                                       the eye in the client world, nearest and within 24, those with
 *                                                       a clear line), an R row every 20,000 steps, an E row at the end
 *                                                       (the first log seen in the cast, targeted, mined, held, the
 *                                                       wooden pickaxe; the farthest from spawn; the surface label's
 *                                                       steps by dispatch branch; wood_search's books); struct wl_acc)
 *                                  [--crossings DIR] (lane/fwd, with --stage-log, attempt 0 only, books only: the attempt's
 *                                                       steps kept, at each first hit its raw macro memory to
 *                                                       DIR/EVENT.mem (neth_vec_chain_memory_dump) and at the end every
 *                                                       step to DIR/seg/1.bin (a segment file, id 1, parent 0))
 *                                  [--replay-steps DIR:N --replay-mem FILE] (lane/fwd, with --rollout-out, no --resume:
 *                                                       the restore of a crossing: attempt 0 plays the first N acts of
 *                                                       DIR/seg/1.bin, then reads FILE's raw macro memory and goes on
 *                                                       live; with the uninterrupted run's --seed and run file every
 *                                                       draw of the driver lines up, so it continues that run exactly)
 *                                  [--replay-reseed S] (lane/choice, with --replay-mem: the live part after the restore
 *                                                       draws the driver from sampler S, attempt 0's rule; a second
 *                                                       sampler block from one crossing)
 *                                  [--choice-log FILE] (lane/choice, with --rollout-out, books only: a C row at each
 *                                                       conflict of the armour craft slot (armor.c arm_choose,
 *                                                       neth_vec_kit's ch_*), an E row an attempt with its totals)
 *                                  [--mem-at N:FILE] (lane/choice, --n 1, books only: attempt 0's raw macro memory at
 *                                                       step N, for --replay-steps; a restore point anywhere)
 *   out/runtime/train/chain path DIR NODE           the chain from the root to NODE: a line a node
 *   out/runtime/train/chain stitch DIR NODE -o SCRIPT [--steps FILE]
 *                                                   NODE's chain as one episode: the Java oracle's script (a Snapshot
 *                                                   line, then each world tick's act, moveact_script_line), and the
 *                                                   policy steps (struct rec_step, the labelled data) to FILE
 *   out/runtime/train/chain check --run FILE DIR NODE [--food-ledger TSV]
 *                                                   the stitched acts played in one env from the run's start (the
 *                                                   binding, no macro, neth_rl_step): at every node's boundary the state
 *                                                   against the one saved there (place, health, food, inventory),
 *                                                   CHECK EQUAL or the first node that differs
 *   out/runtime/train/chain end DIR NODE TAPE       the Java client's last row (the oracle's rows of the stitched
 *                                                   script) against NODE's state: the inventory, END EQUAL or DIFF
 *
 * The search. A node is a saved env state (rlbind.h neth_rl_start_save through vecenv.h neth_vec_chain_save: an image a
 * reset copies in about 29 ms) with the acts that led to it from its parent's state: node 0 is the run's start. Each
 * node has a rung of the ladder (RUNGS below: the wooden pickaxe .. the dragon dead), the highest whose condition its
 * state meets. N envs play segments of S seconds, each from a node the frontier picks (the top rungs' nodes most often,
 * fewest tries and best kept first), with the run's macros and a random driver (brutetab's: each camera bin uniform
 * over 20 +- W, each key held with probability P, a xorshift an env), the first 0..F decisions with no macro
 * (neth_vec_chain_free) so envs that one macro would drive the same way from one state part. An env whose state
 * reaches a rung over the one its segment began at (and over what it saved this segment) is saved as a new node (once
 * no macro runs, or W decisions after the gain), its policy steps written to DIR/seg/ID.bin and macro memory to
 * DIR/seg/ID.mem; a death ends
 * the segment (keep_inventory off: the chain is dead). At most K nodes a rung and L in all keep their images (the
 * worst-kept of the fullest rung let go first, never one an env plays from or a save replays from). DIR/nodes.tsv has
 * a line a node, DIR/frontier.txt the ladder's table, rewritten every --every seconds.
 *
 * Stitching. A node's chain is its parents' back to node 0; an episode from node 0 is, tick by tick, each node's
 * segment in turn: the segment's reset step (no act) and its policy steps. The image a reset copies continues as the
 * saved env would have (neth_rl_start_save's contract), so the stitched acts from the run's start make the same states;
 * `check` proves it in C and `end` on the Java client.
 *
 * The run file is brutetab's (sim, and policy.actions.macros and the macro options); CUDA is never needed on a run file
 * with device_mesh 0, raycast host, render none. Privileged macro options (the oracle_* ones) are the macros' own,
 * passed with --mopt, off by default. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <zstd.h>

#include "../../engine/tape.h"
#include "../../engine/fortress.h"
#include "../moveact.h"
#include "../pool.h"
#include "../rlgui.h"
#include "../vecenv.h"
#include "../endview.h"
#include "../netherview.h"
#include "../shoracle.h"
#include "rec.h"
#include "stages.h"

/* A verifier may load an older variant library while a search keeps running on it. The disk-memory hooks were
 * introduced later; old libraries still stitch and check their segments without them. */
#if defined(__linux__)
#pragma weak neth_vec_chain_memory_read
#pragma weak neth_vec_chain_memory_write
#endif

/* the search's defaults */
#define CH_N 64                 /* envs */
#define CH_GROUPS 4
#define CH_SEG_S 180.0          /* a segment's game seconds */
#define CH_FREE 40              /* a segment's first 0 .. CH_FREE decisions with no macro */
#define CH_KEEP 8               /* nodes a rung keeping their images */
#define CH_LIVE 160             /* nodes keeping their images in all */
#define CH_LIVE_PER_ENV 3       /* ... at least this many an env (lane/chain2; under one an env refused) */
#define CH_ENVS_PER_SLOT 4      /* save slots: one per this many envs, 4 to 32 (--save-slots K sets it) */
#define CH_BEHIND 6             /* without --min-rung, a node this many rungs under the top lets its image go first */
#define CH_SAVE_WAIT 200        /* decisions a rung's gain waits for no macro running before its save anyway */
#define CH_CAM_W 5              /* brutetab's driver: the camera bins 20 +- CH_CAM_W */
#define CH_KEY_P 0.5            /* each key held with this probability */
#define CH_EP_LEN (1 << 30)     /* the vector env's clock: never the end (the search ends every segment itself) */
#define CH_TOP_W 8.0            /* the frontier's rung weights: the top rung present, the next, the next, the rest */
#define CH_NEXT_W 4.0
#define CH_THIRD_W 2.0
#define CH_TRY_W 0.3            /* a node's score: its kept score less CH_TRY_W log2(1 + tries) */
#define CH_EVICT_TRIES 3        /* an End node tried fewer times than this is let go last (lane/chain2) */
#define CH_GONE 10              /* the last nodes let go, kept for frontier.txt */
#define CH_GAIN_CAP 2          /* the eviction score's bonus: 0.5 a segment that saved over the node, at most this many */
#define CH_BEST_P 0.7           /* the best-scored node of the rung, else one at random */
#define CH_DRAGON_HP 200.0
#define CH_CRYSTALS 24          /* over the End's ender crystals (one a pillar, as many as the decorator made): only its rises count */
#define CH_END_HP 8.0           /* an End state is saved at this health or over */
#define CH_ARMOR 13             /* the iron_armor rung: armor points worn (iron boots 2, leggings 5, chestplate 6: 1.7.10's
                                 * ItemArmor reductions; lane/shold's winning End kit) */
#define CH_LAND_Y 64.0          /* the on_land rung: the feet at this height or over (the sea's surface 62) */
#define CH_GAIN_MAX 24          /* --target-gain: the gains whose step --gain-log keeps (12 before lane/bportal: the
                                 * obsidian macro wants 20 with obsidian_carry) */
#define CH_BL_EVERY 5           /* --build-log, --portal-trace (lane/bportal): the frame and portal look every this many
                                 * steps (the client world round the feet, neth_rl_blocks) */
#define CH_BL_R 6               /* the look's box: this far across the feet each way, */
#define CH_BL_DN 5              /* this far under them and */
#define CH_BL_UP 6              /* this far over */
#define CH_MOPT_MAX 128       /* --mopt NAME=WORD given at most (vecopts.c's MO_N is past 32; the bench set with
                                * lane/nleg's options is 65) */
#define CH_SNOW 64            /* the snowballs_64 rung: snowballs held */
#define CH_AT_SP 7.0           /* the at_spawner rung: the remembered spawner this near the feet (blocks) */

/* items */
enum {
    IT_COBBLE = 4, IT_PLANKS = 5, IT_LOG = 17, IT_OBSIDIAN = 49, IT_FURNACE = 61, IT_LOG2 = 162,
    IT_IRON_PICK = 257, IT_FLINT_STEEL = 259, IT_APPLE = 260, IT_COAL = 263, IT_IRON_SWORD = 267,
    IT_WOOD_SWORD = 268, IT_WOOD_PICK = 270, IT_STONE_SWORD = 272, IT_STONE_PICK = 274, IT_DIA_SWORD = 276,
    IT_DIA_PICK = 278, IT_BREAD = 297, IT_PORK = 319, IT_PORK_C = 320, IT_BUCKET = 325, IT_WATER = 326, IT_LAVA = 327,
    IT_FISH = 349, IT_FISH_C = 350, IT_BEEF = 363, IT_BEEF_C = 364, IT_CHICKEN = 365, IT_CHICKEN_C = 366,
    IT_SNOWBALL = 332, IT_FLESH = 367, IT_PEARL = 368, IT_ROD = 369, IT_POWDER = 377, IT_EYE = 381, IT_CARROT = 391, IT_POTATO_B = 393
};
static int snow_name_min = CH_SNOW;

/* the ladder: a rung each, in the order a run reaches them; a state's rung the highest whose condition holds */
enum {
    R_SPAWN, R_WOOD, R_STONE, R_FURNACE, R_MEAT, R_INGOT, R_IRON_PICK, R_BUCKET, R_FLINT, R_WATER, R_DIA_PICK, R_OBS1,
    R_OBS10, R_OBS20, R_PROV, R_NETHER, R_FORT_SEEN, R_ON_FORT, R_AT_SP, R_ROD1, R_ROD7 = R_ROD1 + 6, R_HOME, R_LAND, R_ARMOR, R_PEARL1, R_PEARL12 = R_PEARL1 + 11,
    R_EYE12, R_SWORD, R_FOOD_STOCK, R_SNOW, R_SH_ROOM, R_FILLED, R_END, R_D75, R_D50, R_D25, R_DEAD, R_N
};

static void rung_name(int r, char *out, size_t n)
{
    static const char *const fixed[] = {
        [R_SPAWN] = "spawn", [R_WOOD] = "wooden_pickaxe", [R_STONE] = "stone_pickaxe", [R_FURNACE] = "furnace", [R_MEAT] = "meat",
        [R_INGOT] = "iron_ingot", [R_IRON_PICK] = "iron_pickaxe", [R_BUCKET] = "bucket", [R_FLINT] = "flint_and_steel",
        [R_WATER] = "water_bucket", [R_DIA_PICK] = "diamond_pickaxe", [R_OBS1] = "obsidian_1",
        [R_OBS10] = "obsidian_10", [R_OBS20] = "obsidian_20", [R_PROV] = "provisions", [R_NETHER] = "nether", [R_FORT_SEEN] = "fortress_seen", [R_ON_FORT] = "on_fortress", [R_AT_SP] = "at_spawner",
        [R_HOME] = "overworld_7_rods", [R_LAND] = "on_land",[R_EYE12] = "eyes_12", [R_SWORD] = "sword", [R_FOOD_STOCK] = "food_stock", [R_ARMOR] = "iron_armor", [R_SNOW] = "snowballs_64", [R_SH_ROOM] = "stronghold_room",
        [R_FILLED] = "portal_filled", [R_END] = "end", [R_D75] = "dragon_75", [R_D50] = "dragon_50",
        [R_D25] = "dragon_25", [R_DEAD] = "dragon_dead"};
    if (r >= R_ROD1 && r <= R_ROD7) snprintf(out, n, "rod_%d", r - R_ROD1 + 1);
    else if (r >= R_PEARL1 && r <= R_PEARL12) snprintf(out, n, "pearl_%d", r - R_PEARL1 + 1);
    else if (r == R_SNOW) snprintf(out, n, "snowballs_%d", snow_name_min);
    else if (r >= 0 && r < R_N && fixed[r]) snprintf(out, n, "%s", fixed[r]);
    else snprintf(out, n, "?%d", r);
}

static int rung_by_name(const char *s)
{
    if (!strcmp(s, "snowballs_16") || !strcmp(s, "snowballs_32") || !strcmp(s, "snowballs_64")) return R_SNOW;
    char b[64];
    for (int r = 0; r < R_N; ++r)
    {
        rung_name(r, b, sizeof b);
        if (!strcmp(b, s)) return r;
    }
    char *e;
    long v = strtol(s, &e, 10);
    return *s && !*e && v >= 0 && v < R_N ? (int)v : -1;
}

static int inv_n(const struct neth_rl_state *s, int item)
{
    int n = 0;
    for (int j = 0; j < 36; ++j)
        if (s->inv[j].item == item && s->inv[j].count > 0) n += s->inv[j].count;
    return n;
}

/* PICKS_HELD (lane/supply, books only): the pickaxes held, wood stone iron diamond gold, a digit each, H a hotbar's and
 * M the main inventory's (H1100 M0010: wood and stone in the hotbar, iron in the main inventory) */
static void picks_held(const struct neth_rl_state *s, char ph[16])
{
    static const int PK[5] = {270, 274, 257, 278, 285};
    ph[0] = 'H', ph[6] = ' ', ph[7] = 'M', ph[13] = 0;
    for (int k = 0; k < 5; ++k)
    {
        int hb = 0, mn = 0;
        for (int i = 0; i < 36; ++i)
            if (s->inv[i].item == PK[k] && s->inv[i].count > 0) *(i < 9 ? &hb : &mn) += 1;
        ph[1 + k] = (char)('0' + (hb > 9 ? 9 : hb)), ph[8 + k] = (char)('0' + (mn > 9 ? 9 : mn));
    }
}

/* the most uses left on a pickaxe that digs iron ore (stone 131, iron 250, diamond 1561, less its damage); -1 none
 * (lane/supply, books only: the portal trace's pk_left) */
static int pick_left(const struct neth_rl_state *s)
{
    int best = -1;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, max = it == 274 ? 131 : it == 257 ? 250 : it == 278 ? 1561 : 0;
        if (max && s->inv[i].count > 0 && max - s->inv[i].damage > best) best = max - s->inv[i].damage;
    }
    return best;
}

/* --target-gain KIND (lane/rods): what an attempt gains, counted on the inventory: TG_ROD as rung_of's rod_eq (rods, and
 * powder and eyes at two a rod, so crafting powder is no loss), TG_PEARL pearls and eyes (lane/proom's), TG_OBS
 * obsidian held (lane/bportal: a frame placed spends it, so the most held is the count), TG_ARMOR armour points worn
 * (lane/supply: the armour slots, 1.7.10's points a piece) */
enum { TG_PEARL = 0, TG_ROD = 1, TG_OBS = 2, TG_ARMOR = 3 };
static int armor_pts(const struct neth_rl_state *s)
{
    static const int AP[20] = {1, 3, 2, 1, 2, 5, 4, 1, 2, 6, 5, 2, 3, 8, 6, 3, 2, 5, 3, 1}; /* 298 .. 317 */
    int pts = 0;
    for (int k = 0; k < 4; ++k)
    {
        int it = s->armor[k].item;
        if (it >= 298 && it <= 317) pts += AP[it - 298];
    }
    return pts;
}
static int gain_count(const struct neth_rl_state *s, int kind)
{
    if (kind == TG_ARMOR) return armor_pts(s);
    if (kind == TG_ROD) return inv_n(s, IT_ROD) + (inv_n(s, IT_POWDER) + inv_n(s, IT_EYE) + 1) / 2;
    if (kind == TG_OBS) return inv_n(s, IT_OBSIDIAN);
    return inv_n(s, IT_PEARL) + inv_n(s, IT_EYE);
}

/* --build-log, --portal-trace (lane/bportal, books only): the client world round the feet (what the player has seen,
 * neth_rl_blocks), CH_BL_R across, CH_BL_DN under and CH_BL_UP over: the most of a Nether frame's ten cells (the corners
 * not needed in 1.7.10: two across the bottom and the top, three up each side of a 2 by 3 inside) one frame there has
 * in obsidian, and the portal blocks (90) there; *cells -1 when the box could not be read */
static void build_look(struct nwrl *h, int id, double x, double y, double z, int *cells, int *portals)
{
    enum { W = 2 * CH_BL_R + 1, H = CH_BL_DN + CH_BL_UP + 1 };
    static __thread int16_t b[W * W * H];
    int x0 = (int)floor(x) - CH_BL_R, y0 = (int)floor(y + 0.01) - CH_BL_DN, z0 = (int)floor(z) - CH_BL_R;
    *cells = -1, *portals = 0;
    if (y0 < 0) y0 = 0;
    if (y0 + H - 1 > 255) y0 = 256 - H;
    if (neth_rl_blocks(h, id, x0, y0, z0, x0 + W - 1, y0 + H - 1, z0 + W - 1, b) != 0) return;
#define BL(i, j, k) ((i) < 0 || (j) < 0 || (k) < 0 || (i) >= W || (j) >= H || (k) >= W ? -1 : b[(i) + W * ((k) + W * (j))])
    int best = 0;
    for (int j = 0; j < H; ++j)
        for (int k = 0; k < W; ++k)
            for (int i = 0; i < W; ++i)
            {
                *portals += BL(i, j, k) == 90;
                /* (i, j, k) the inside's low corner, the frame along x (dx 1) or along z */
                for (int o = 0; o < 2; ++o)
                {
                    int dx = o == 0, dz = o == 1, n = 0;
                    for (int a = 0; a < 2; ++a)
                        n += (BL(i + a * dx, j - 1, k + a * dz) == 49) + (BL(i + a * dx, j + 3, k + a * dz) == 49);
                    for (int u = 0; u < 3; ++u)
                        n += (BL(i - dx, j + u, k - dz) == 49) + (BL(i + 2 * dx, j + u, k + 2 * dz) == 49);
                    if (n > best) best = n;
                }
            }
#undef BL
    *cells = best;
}

static void sword_gear(const struct neth_rl_state *s, int *count, int *fresh, int *held_wear)
{
    *count = *fresh = 0, *held_wear = -1;
    for (int j = 0; j < 36; ++j)
    {
        int it = s->inv[j].item;
        if (s->inv[j].count <= 0 || (it != 276 && it != 267 && it != 272 && it != 268 && it != 283)) continue;
        *count += s->inv[j].count;
        *fresh += s->inv[j].damage == 0 ? s->inv[j].count : 0;
        if (j == s->current) *held_wear = s->inv[j].damage;
    }
}

/* the hunger points of the food held (1.7.10's ItemFood heal amounts; rotten flesh none) */
static int food_points(const struct neth_rl_state *s)
{
    static const int16_t H[][2] = {{320, 8}, {364, 8}, {400, 8}, {366, 6}, {282, 6}, {393, 6}, {297, 5}, {350, 5},
                                   {260, 4}, {391, 4}, {322, 4}, {363, 3}, {319, 3}, {365, 2}, {349, 2}, {360, 2},
                                   {357, 2}, {392, 1}};
    int n = 0;
    for (size_t k = 0; k < sizeof H / sizeof *H; ++k) n += H[k][1] * inv_n(s, H[k][0]);
    return n;
}

/* lane/supply (--supply-log, books only): a row of what the player holds, all of it the server's state (the
 * inventory and the armour slots, rlbind.h neth_rl_state.armor, read by no macro): ATTEMPT RESULT ROW (entry: the first
 * Nether step; end: the attempt's last) STEP DIM HEALTH FOOD FPTS (food_points: every food item, raw meat included)
 * COOKED (the cooked meat, bread and apples among them) ARMOR (points of the pieces worn: 1.7.10's ItemArmor
 * damageReduceAmount) then each slot's item (-1 none) and damage (boots, leggings, chestplate, helmet), SWORD (the best
 * held, 0 none), BLOCKS (cobblestone, dirt, netherrack, planks and stone held: what the macros place), BUCKET WATER
 * LAVA (buckets held), INGOT (iron ingots), ORE (iron ore), OBS, then the armour macro's books (neth_vec_kit: ores dug,
 * the first ore's step, the step four pieces were worn, -1 not yet), ARM_STEPS (the overworld steps it drove) and its
 * ore targets: PICKS, GU_NOWAY (given up with no cell walked: the planner found no way), GU_WALKED. ROW
 * alive: a death's last living step (its end row holds what the death left: nothing). supply_fmt writes the row from
 * STEP on into buf */
#define SP_ROW 512
/* lane/supply (the armour leg's ladder, books only): the first step of each rung, -1 never: the armour macro's first
 * step, an ore picked, an ore dug (iron or armor_fuel's coal), 24 iron held (ingots, iron ore and the pieces held or
 * worn at their cost: helmet 5, chestplate 8, leggings 7, boots 4), a placed furnace in reach with fuel held (a coal or
 * two planks), 24 ingots (the same without the ore), each piece held or worn (boots, leggings, chestplate, helmet),
 * each piece worn */
enum { SPR_TURN, SPR_PICK, SPR_DUG, SPR_IRON24, SPR_FURN, SPR_ING24, SPR_HAS, SPR_WORN = SPR_HAS + 4, SPR_N = SPR_WORN + 4 };
static void supply_rungs(int64_t *rg, int64_t step, const struct neth_rl_state *s, const struct neth_vec_kit *kit,
                         int64_t arm_steps)
{
    static const int PIECE[4] = {309, 308, 307, 306}, COST[4] = {4, 7, 8, 5};
    int has = 0, worn = 0, cost = 0;
    for (int k = 0; k < 4; ++k)
    {
        int w = 0;
        for (int j = 0; j < 4; ++j) w |= s->armor[j].item == PIECE[k];
        int h = w || inv_n(s, PIECE[k]) > 0;
        if (w) worn |= 1 << k;
        if (h) has |= 1 << k, cost += COST[k];
    }
    int ing = inv_n(s, 265), ore = inv_n(s, 15);
    int fuel = inv_n(s, 263) > 0 || inv_n(s, 5) >= 2;
    int hit[SPR_N] = {arm_steps > 0, kit->picks > 0, kit->ores_dug > 0, ing + ore + cost >= 24,
                      kit->furnace_near && fuel, ing + cost >= 24};
    for (int k = 0; k < 4; ++k) hit[SPR_HAS + k] = (has >> k) & 1, hit[SPR_WORN + k] = (worn >> k) & 1;
    for (int k = 0; k < SPR_N; ++k)
        if (hit[k] && rg[k] < 0) rg[k] = step;
}

static void supply_fmt(char *buf, int64_t step, const struct neth_vec_chain_peek *p, const struct neth_vec_kit *kit,
                       int64_t arm_steps, const int64_t *rg)
{
    static const int sw[] = {276, 267, 272, 283, 268};
    static const int cooked[][2] = {{320, 8}, {364, 8}, {366, 6}, {350, 5}, {297, 5}, {260, 4}, {393, 6}, {400, 8}};
    const struct neth_rl_state *s = &p->st;
    int pts = armor_pts(s), best = 0, ck = 0;
    for (int k = 0; k < 5 && !best; ++k)
        if (inv_n(s, sw[k]) > 0) best = sw[k];
    for (size_t k = 0; k < sizeof cooked / sizeof *cooked; ++k) ck += cooked[k][1] * inv_n(s, cooked[k][0]);
    int blocks = inv_n(s, 4) + inv_n(s, 3) + inv_n(s, 87) + inv_n(s, 5) + inv_n(s, 1);
    int n = snprintf(buf, SP_ROW, "%lld\t%d\t%.1f\t%d\t%d\t%d\t%d", (long long)step, p->dim, p->health, p->food,
                     food_points(s), ck, pts);
    for (int k = 0; k < 4; ++k)
        n += snprintf(buf + n, SP_ROW - (size_t)n, "\t%d:%d", s->armor[k].item,
                      s->armor[k].item >= 0 ? s->armor[k].damage : -1);
    n += snprintf(buf + n, SP_ROW - (size_t)n, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%d\t%d\t%d",
                  best, blocks, inv_n(s, 325), inv_n(s, 326), inv_n(s, 327), inv_n(s, 265), inv_n(s, 15), inv_n(s, 49),
                  kit->ores_dug, (long long)kit->t_ore1, (long long)kit->t_worn4, (long long)arm_steps, kit->picks,
                  kit->gu_noway, kit->gu_walked);
    for (int k = 0; k < SPR_N; ++k) n += snprintf(buf + n, SP_ROW - (size_t)n, "\t%lld", (long long)rg[k]);
    int free_slots = 0;     /* FREE: the main inventory's empty slots (a full one leaves an ore's drop on the ground) */
    for (int i = 0; i < 36; ++i) free_slots += s->inv[i].item < 0 || s->inv[i].count <= 0;
    char ph[16];
    picks_held(s, ph);
    snprintf(buf + n, SP_ROW - (size_t)n, "\t%d\t%s", free_slots, ph);
}

static void supply_row(FILE *f, int i, const char *result, const char *row, const char *buf)
{
    fprintf(f, "%d\t%s\t%s\t%s\n", i, result, row, buf);
    fflush(f);
}

/* what a state's rung is read from: the peek and the macros' books of the episode so far */
struct view {
    struct neth_vec_chain_peek p;
    int fort_seen, on_fort, sh_room, sh_lit, at_sp;
    int st_room, st_lit;        /* lane/proom: the stronghold book's room and lit events whatever dimensions the episode
                                 * carries (stage_hit's: a library start past the Nether carries no Nether visit, so
                                 * sh_room and sh_lit, read only after one, never held there) */
    double home_d;             /* in the Nether: blocks from the feet to the arrival portal's cell (0: none known) */
    double loc_d, loc_d0;       /* privileged start chunk: current and arrival distances before a piece is loaded */
    double entry_d;             /* privileged enclosed piece's standing cell, including height */
    double dragon_hp;           /* -1 none in view */
    int crystals;               /* in the End: crystals taken (neth_vec_dragon_info [11] arrows' hits + [73] lobs') */
    int cry_gone;               /* lane/chain2: the saved parent's crystal hits plus this segment's new hits */
    int prov;                   /* --provisions P: the chain held P hunger points of food at a state with the portal's
                                 * kit (this one or an ancestor; always 1 without the flag) */
    int prov_on;                /* the flag given (the provisions rung then counts) */
    int meat, meat_on, meat_target; /* --meat M: likewise M meats (raw or cooked pork, beef, chicken) held after the furnace */
    int water_kit_on;             /* --water-seg: save held wood, planks, table and sticks after three diamonds */
    int toward_on;              /* --toward X,Z given */
    double toward_d;            /* blocks (horizontally) from the feet to it */
    int chamber_stronghold;     /* option-specific seven-rod progress in Nether blocks */
    int kit_on;                 /* --kit: the End's kit (iron armor worn, 64 snowballs) before the stronghold */
    int snow_min;               /* --snow-min 16|32|64: held balls that count as the snow rung (64 before this option) */
    int armor;                  /* armor points worn (0: not read yet: lane/rod's armor books) */
    int armor_min;              /* --armor-min: the points that count as ready for the later rungs */
    int carry, carry_n;         /* --carry N: the chain held N obsidian with the flint and steel (this state or an
                                 * ancestor; 1 without the flag), and N (0: the flag not given) */
    int obs_health_min, obs_food_min; /* optional floor before an obsidian/provisions rung counts */
    /* lane/chain2: progress inside the rungs that had none */
    double goal_d;              /* in the Nether: blocks to the remembered spawner, else the fortress cell (-1 neither) */
    int sh_filled;              /* eyes put in frames (the stronghold macro's books) */
    int endfood;                /* --endfood P: food points held before anything past the sword's rung counts (0 off) */
    int post_stock_score;       /* --post-stock-food: one travel stride across a post-stock meal */
    int provision_on;           /* food_provision=on or armor_first: a rung at the stock gate before snowballs */
    int late_health_min, late_food_min; /* optional floor before the snow and room rungs can outrank a fed sibling */
};

static int meat_n(const struct neth_rl_state *s)
{
    return inv_n(s, IT_PORK) + inv_n(s, IT_PORK_C) + inv_n(s, IT_BEEF) + inv_n(s, IT_BEEF_C) + inv_n(s, IT_CHICKEN) +
           inv_n(s, IT_CHICKEN_C);
}

static int rung_of(const struct view *w)
{
    const struct neth_vec_chain_peek *p = &w->p;
    const struct neth_rl_state *s = &p->st;
    uint32_t ms = p->ms;
    int r = R_SPAWN;
#define AT(c, k) do { if (c) r = (k) > r ? (k) : r; } while (0)
    AT(ms & 1u, R_WOOD);
    AT(ms >> 1 & 1u, R_STONE);
    AT(ms >> 4 & 1u, R_FURNACE);
    AT((ms >> 4 & 1u) && w->meat && w->meat_on, R_MEAT);
    AT(ms >> 5 & 1u, R_INGOT);
    AT(ms >> 2 & 1u, R_IRON_PICK);
    AT(ms >> 6 & 1u, R_BUCKET);
    AT(ms >> 7 & 1u, R_FLINT);
    AT((ms >> 7 & 1u) && inv_n(s, IT_WATER) > 0, R_WATER);
    /* the portal's kit held from the diamond pickaxe on (a bucket of any kind, the flint and steel): a diamond pickaxe
     * without it is a dead end the frontier kept (e42a, easy: 451 such states, iron ore held and no furnace) */
    int fs = inv_n(s, IT_FLINT_STEEL) > 0, kit = fs && inv_n(s, IT_BUCKET) + inv_n(s, IT_WATER) + inv_n(s, IT_LAVA) > 0;
    /* ... and the bucket full (e42b, s42l: 1,429 of 1,463 diamond pickaxes with an empty bucket in the mine, where the
     * obsidian macro found no water in 15 minutes): the water before the diamonds, as s42e's chains had it */
    AT((ms >> 3 & 1u) && fs && inv_n(s, IT_WATER) + inv_n(s, IT_LAVA) > 0, R_DIA_PICK);
    int obs = inv_n(s, IT_OBSIDIAN);
    int obs_ready = p->health >= w->obs_health_min && food_points(s) >= w->obs_food_min;
    AT(obs >= 1 && kit && obs_ready, R_OBS1);
    AT(obs >= 10 && fs && obs_ready, R_OBS10);
    /* --carry N: the way home carried (obsidian_carry, nether_portal_home): N held once, 10 still held */
    AT(obs >= 10 && fs && w->carry && w->carry_n && obs_ready, R_OBS20);
    AT(obs >= 10 && w->prov && w->prov_on && obs_ready, R_PROV);
    AT(p->dim == -1 || (p->dims & 1), R_NETHER);
    AT(p->dim == -1 && w->fort_seen, R_FORT_SEEN);
    AT(p->dim == -1 && w->on_fort, R_ON_FORT);
    AT(p->dim == -1 && w->at_sp, R_AT_SP);
    int rods = inv_n(s, IT_ROD), powder = inv_n(s, IT_POWDER), eyes = inv_n(s, IT_EYE), pearls = inv_n(s, IT_PEARL);
    int rod_eq = rods + (powder + eyes + 1) / 2;
    for (int k = 1; k <= 7; ++k) AT(rod_eq >= k && (p->dims & 1), R_ROD1 + k - 1);
    AT(p->dim == 0 && (p->dims & 1) && rod_eq >= 7, R_HOME);
    /* --toward X,Z: on land near the way (the second portal came out under an ocean: e42y's dusk states swimming at
     * y 61, the hunt never begun): feet at y CH_LAND_Y or over with full air */
    int land = p->dim == 0 && (p->dims & 1) && rod_eq >= 7 && w->toward_on && p->y >= CH_LAND_Y && p->air >= 300;
    AT(land, R_LAND);
    /* the End's kit (lane/shold's End cases: no kill without iron armor worn; the crystals by snowballs; the
     * orchestrator, 5:46 am: the armor by day before the pearl nights): armor points worn (lane/rod's neth_vec_kit) and
     * the snowballs held */
    int snow_min = w->snow_min ? w->snow_min : CH_SNOW;
    int arm = w->armor >= (w->armor_min ? w->armor_min : CH_ARMOR), snw = inv_n(s, IT_SNOWBALL) >= snow_min;
    AT(p->dim == 0 && (p->dims & 1) && rod_eq >= 7 && arm, R_ARMOR);
    int pearl_eq = pearls + eyes;
    for (int k = 1; k <= 12; ++k) AT(p->dim != -1 && (p->dims & 1) && rod_eq >= 6 && pearl_eq >= k, R_PEARL1 + k - 1);
    AT(eyes >= 12, R_EYE12);
    int sword = inv_n(s, 276) + inv_n(s, 267) + inv_n(s, 272) + inv_n(s, 268) + inv_n(s, 283) > 0 ||
                (inv_n(s, 261) > 0 && inv_n(s, 262) > 0);
    AT(eyes >= 12 && sword, R_SWORD);
    AT(w->provision_on && eyes >= 12 && sword && arm && food_points(s) >= 160 &&
       inv_n(s, IT_FURNACE) > 0 && p->y >= CH_LAND_Y, R_FOOD_STOCK);
    AT(eyes >= 12 && snw, R_SNOW);
    AT(w->sh_room, R_SH_ROOM);
    AT(w->sh_lit, R_FILLED);
    AT(p->dim == 1, R_END);
    AT(p->dim == 1 && w->dragon_hp >= 0 && w->dragon_hp <= 0.75 * CH_DRAGON_HP, R_D75);
    AT(p->dim == 1 && w->dragon_hp >= 0 && w->dragon_hp <= 0.50 * CH_DRAGON_HP, R_D50);
    AT(p->dim == 1 && w->dragon_hp >= 0 && w->dragon_hp <= 0.25 * CH_DRAGON_HP, R_D25);
    AT(ms >> NETH_VEC_MS_DRAGON & 1u, R_DEAD);
#undef AT
    /* --meat, --provisions: no rung past the food's for a chain that never held it (the mine entered with no meat, the
     * Nether entered starving: no gain; such states rank at the rung under the food's) */
    if (w->meat_on && !w->meat && r > R_MEAT) r = R_FURNACE;
    if (w->prov_on && !w->prov && r > R_PROV) r = w->carry_n && w->carry ? R_OBS20 : R_OBS10;
    if (w->carry_n && !w->carry && r > R_OBS20) r = R_OBS10;
    /* --kit: nothing past the snowballs' rung counts without the End's kit (the stronghold held until it is made) */
    /* --kit: no pearl rung without the armor (it is bought by day first), nothing past the snowballs' without them */
    /* (the kit's gates are the way into the End's, not the fight's: in the End the snowballs are thrown at the crystals,
     * and with them gone every fighting state ranked under its End node, so no gain was possible: lane/chain2, parts
     * 18 to 20, the dragon at 150 or under in 185 segments and no node saved) */
    int out_end = p->dim != 1;
    if (w->kit_on && r > R_ARMOR && !arm && out_end) r = land ? R_LAND : R_HOME;
    if (w->kit_on == 1 && r > R_SNOW && !snw && out_end) r = sword ? R_SWORD : R_EYE12;    /* (--kit 2: the armor only) */
    /* --kit 1: nothing past the eyes without a sword (or a bow and arrows), its own rung (lane/chain2): the dragon
     * macro's dragon_want needs one in the hotbar, and the chain's swords were gone by the End (e43z: 191 End
     * segments, the macro off in every one, 180 deaths near the void with the policy driving) */
    if (w->kit_on == 1 && r > R_EYE12 && !sword && out_end) r = R_EYE12;
    int late_stock_floor = w->post_stock_score ? w->endfood : 160;
    if (w->provision_on && r >= R_SNOW && food_points(s) < late_stock_floor && out_end)
        r = eyes >= 12 && sword ? R_SWORD : R_EYE12;
    if (out_end && r >= R_SNOW &&
        ((w->late_health_min > 0 && p->health < w->late_health_min) ||
         (w->late_food_min > 0 && food_points(s) < w->late_food_min)))
        r = sword ? R_SWORD : R_EYE12;
    /* --endfood P (lane/chain2): nothing past the snowballs without P hunger points of food held: the End's fight
     * regenerates only at food 18 (lane/chain's dragon deaths came at food 13 to 15) */
    if (w->endfood > 0 && r > R_SNOW && p->dim != 1 && food_points(s) < w->endfood) r = R_SNOW;
    return r;
}

/* --target-stage NAME[:BASE] (lane/legs): a library leg's end, the next stage as stages.h names it, read on the live
 * state and past none of the ladder's gates (the kit, the food, the meat): a milestone item (the run's milestone
 * mask), a dimension being there, the stronghold's room and its lit portal (the chain's sh_room and sh_lit), the
 * crystals gone (BASE, those a crystals_goneM start had, plus the attempt's own), the dragon's health at or under 150,
 * 100, 50 (dragon_75, 50, 25), its death */
static int stage_hit(const struct view *w, int j, int base)
{
    const struct stage_ent *s = &STAGE_CHAIN[j];
    const struct neth_vec_chain_peek *p = &w->p;
    if (s->kind == STAGE_MS || s->kind == STAGE_DRAGON) return (p->ms >> s->v & 1u) != 0;
    /* (lane/bportal: a dimension's BASE, that many obsidian held there too: nether:10, the Nether with its way home) */
    if (s->kind == STAGE_DIM) return p->dim == s->v && inv_n(&p->st, IT_OBSIDIAN) >= base;
    if (!strcmp(s->name, "stronghold_room")) return w->st_room;
    if (!strcmp(s->name, "portal_lit")) return w->st_lit;
    if (!strncmp(s->name, "crystals_gone", 13)) return p->dim == 1 && base + w->cry_gone >= atoi(s->name + 13);
    if (!strncmp(s->name, "dragon_", 7))
        return p->dim == 1 && w->dragon_hp >= 0 && w->dragon_hp <= CH_DRAGON_HP * atoi(s->name + 7) / 100.0;
    return 0;
}

/* the progress inside a rung: what the next rungs are made of, held (iron ore and ingots toward the bucket and flint and
 * steel, flint, diamonds, obsidian, blaze kills' rods are rungs already); a state that gains CH_PROG_STEP of it over its
 * segment's best is saved at the same rung */
#define CH_PROG_STEP 1
#define CH_WAIT_T 2400          /* world ticks a step of the pearls' night wait (2 game minutes) */
#define CH_DAY0 500             /* --dayff: a pearl node's day, the world's clock from here ... */
#define CH_DUSK 12000           /* ... to here (the hunt wanted from PEARL_NIGHT0 less its lead); its segment runs to */
#define CH_DUSK_END 12400       /* here, past the last CH_WAIT_T gain (12000) and its save wait */
#define CH_PROG_W 0.15f         /* a node's score: + CH_PROG_W progress */
#define CH_PROG_W_END 1.0f      /* ... in the End */
#define CH_WATER_LOG_CAP 2
#define CH_WATER_PLANK_CAP 8
#define CH_WATER_TABLE_CREDIT 6
#define CH_WATER_STICK_CAP 2
#define CH_WATER_STICK_CREDIT 2
static int prog_of(const struct view *w, int rung)
{
    const struct neth_rl_state *s = &w->p.st;
    /* --meat: the meat held is the progress up to the meat rung (a hunt's state saved before the furnace is made) */
    if (w->meat_on && !w->meat && rung >= R_WOOD && rung <= R_FURNACE)
    {
        int m = meat_n(s);
        return m > w->meat_target ? w->meat_target : m;
    }
    /* the way home: each 8 blocks nearer the arrival portal's cell (within 96) is progress at rod_7 (the orchestrator,
     * 4:54 am: lane/rod's sealed departure and tunnel get a weak player 19 blocks over the portal, then stall) */
    if (rung == R_ROD7 && w->p.dim == -1 && w->chamber_stronghold && w->toward_on)
        return (int)((4096.0 - fmin(w->toward_d, 4096.0)) / 8.0);
    if (rung == R_ROD7 && w->p.dim == -1 && w->home_d > 0)
        return (int)((96.0 - (w->home_d < 96.0 ? w->home_d : 96.0)) / 8.0);
    /* A saved node restores the world, then starts a fresh macro with a fresh distance-to-target counter. Score the
     * absolute distance so a child that moves closer stays ahead of its parent across arbitrarily many segments. */
    if (rung == R_NETHER && w->p.dim == -1 && w->loc_d >= 0)
        return (int)((4096.0 - fmin(w->loc_d, 4096.0)) / 8.0);
    if (rung == R_FORT_SEEN && w->p.dim == -1 && w->entry_d >= 0)
        return (int)((4096.0 - fmin(w->entry_d, 4096.0)) / 4.0);
    /* the pearls' wait for the night: each CH_WAIT_T world ticks is progress from home to the twelfth pearl (the hunt
     * starts at night; e42w's home state at tick 21,741 of its day, 505 segments of 3,600 ticks and no state saved) */
    /* --toward: home and on land, each 16 blocks nearer the target (the stronghold) is progress, with the night's wait
     * (the orchestrator, 5:30 am: travel toward the stronghold by day, hunt from land at night) */
    /* --kit: the armor's iron and pieces too (ingots and ore held to 24, two a point worn) */
    int kitp = 0;
    if (w->kit_on && (rung == R_HOME || rung == R_LAND))
    {
        /* (lane/chain2: each step of the kit strictly more than the last: an ore 1, an ingot 2, a piece crafted 3 an
         * ingot it cost, a piece worn 7 a point; and until the armor is worn the kit alone, no clock and no distance:
         * s42k3's on_land frontier stood 100,000 ticks at the stronghold starving, a point every two game minutes,
         * with 18 ore it never smelted) */
        int ore = inv_n(s, 15), ing = inv_n(s, 265);
        int held = 4 * inv_n(s, 309) + 7 * inv_n(s, 308) + 8 * inv_n(s, 307) + 5 * inv_n(s, 306);
        kitp = (ore > 24 ? 24 : ore) + 2 * (ing > 24 ? 24 : ing) + 3 * held + 7 * w->armor;
        if (w->armor < (w->armor_min ? w->armor_min : CH_ARMOR)) return kitp;
    }
    if ((rung == R_HOME || rung == R_LAND) && w->toward_on && w->p.dim == 0)
        return (int)((4096.0 - fmin(w->toward_d, 4096.0)) / 16.0) + (int)(w->p.world_time / CH_WAIT_T) + kitp;
    if (w->provision_on && rung == R_PEARL12 && w->p.dim == 0 &&
        inv_n(s, IT_PEARL) + inv_n(s, IT_EYE) >= 12 &&
        inv_n(s, 276) + inv_n(s, 267) + inv_n(s, 272) + inv_n(s, 268) + inv_n(s, 283) > 0 &&
        w->armor >= w->armor_min)
        return food_points(s); /* twelve pearls before their eyes: stock gains still save */
    /* (--toward: at the pearl rungs each 64 blocks nearer the stronghold too: the last walk shorter; the orchestrator,
     * 6:09 am) */
    if (rung >= R_HOME && rung < R_EYE12 && w->p.dim == 0)
        return (int)(w->p.world_time / CH_WAIT_T) +
               (w->toward_on ? (int)((4096.0 - fmin(w->toward_d, 4096.0)) / 64.0) : 0);
    /* the End: each tenth of the dragon's health taken is progress (lane/shold's fight: the crystals fall to snowball
     * lobs first, its first kills near step 29,000 after arrival: --late-seg holds that in one segment) */
    if (rung >= R_END && rung < R_DEAD && w->p.dim == 1)
    {
        /* (the crystals taken are the segment's own books: the env's reset clears e->dg, so only the dragon's health,
         * the world's, is a progress a child can be held to) */
        double hp = w->dragon_hp >= 0 ? w->dragon_hp : CH_DRAGON_HP;
        /* (lane/chain2: and each crystal gone, which the dragon's healing does not take back) */
        return (int)((CH_DRAGON_HP - hp) / (CH_DRAGON_HP / 10.0)) + w->cry_gone;
    }
    /* (lane/chain2, --endfood: from eyes_12 to the stronghold room the food points held alone until the floor, cooked
     * worth more than raw so a cook is a gain; no clock, no distance before it: s42k3f-8 saved 4,793 snowballs_64
     * nodes in 15 minutes on the distance, holding 0 to 7 cooked meats) */
    if (w->endfood > 0 && rung >= R_EYE12 && rung < R_SNOW && w->p.dim == 0)
    {
        int fp = food_points(s);
        if (fp < w->endfood) return fp;
    }
    /* --kit 1: the snowballs held are progress at eyes_12 (4 a step, to 64: the trip is longer than a segment; e43x,
     * 728 segments from its eyes_12 states, none saved) */
    if ((rung == R_EYE12 || rung == R_SWORD) && w->kit_on == 1)
    {
        int sb = inv_n(s, IT_SNOWBALL);
        /* (at eyes_12 with no sword: the iron toward one too, two ingots) */
        int fe = rung == R_EYE12 ? inv_n(s, 265) : 0;
        int need = w->snow_min ? w->snow_min : CH_SNOW;
        return (sb > need ? need : sb) / 4 + (fe > 2 ? 2 : fe);
    }
    /* lane/chain2: the rungs that had no progress. The snowballs to the stronghold room: each 16 blocks nearer
     * --toward's place, each 4 blocks down within 48 of it, and the food toward --endfood */
    if (rung == R_SNOW && w->p.dim == 0)
    {
        int pr = 0;
        int fp = w->endfood > 0 ? food_points(s) : 0;
        /* Before the food floor, a 64-block travel gain saves the distant snow source without flooding the
         * frontier with every 16-block step. Food still contributes one point per held hunger point. */
        int stride = w->post_stock_score ? 16 : w->endfood > 0 && fp < w->endfood ? 64 : 16;
        if (w->toward_on)
        {
            pr += (int)((4096.0 - fmin(w->toward_d, 4096.0)) / stride);
            if (w->toward_d < 48.0 && w->p.y < 64.0) pr += (int)((64.0 - w->p.y) / 4.0);
        }
        if (w->endfood > 0)
            pr += fp > w->endfood ? w->endfood : fp;
        return pr;
    }
    /* the room to the portal lit: each eye put in a frame */
    if (rung == R_SH_ROOM) return w->sh_filled > 12 ? 12 : w->sh_filled;
    /* the Nether to the seventh rod: each 8 blocks nearer the spawner remembered, else the fortress cell (within 128) */
    if (rung >= R_NETHER && rung < R_ROD7 && w->p.dim == -1 && w->goal_d >= 0)
        return (int)((128.0 - fmin(w->goal_d, 128.0)) / 8.0);
    /* the first rungs without --meat: the wood and stone held */
    if (rung < R_INGOT && !w->meat_on)
    {
        int wd = inv_n(s, 17) + inv_n(s, 162) + inv_n(s, 5) / 4, cb = inv_n(s, IT_COBBLE);
        return (wd > 8 ? 8 : wd) + (cb > 16 ? 16 : cb) / 4;
    }
    if (rung < R_INGOT || rung >= R_NETHER) return 0;
    int iron = inv_n(s, 265) + inv_n(s, 15), dia = inv_n(s, 264), flint = inv_n(s, 318), obs = inv_n(s, IT_OBSIDIAN);
    int woodp = 0;
    if (w->water_kit_on && rung == R_WATER && dia >= 3 && inv_n(s, IT_DIA_PICK) == 0)
    {
        /* The trip to a surface tree is longer than the normal segment. Save its held kit as logs become planks and
         * a table, then let --water-seg cover the table's placement through the pickaxe craft. */
        int logs = inv_n(s, 17) + inv_n(s, 162), planks = inv_n(s, 5), sticks = inv_n(s, 280);
        woodp = (logs > CH_WATER_LOG_CAP ? CH_WATER_LOG_CAP : logs) +
                (planks > CH_WATER_PLANK_CAP ? CH_WATER_PLANK_CAP : planks) +
                CH_WATER_TABLE_CREDIT * (inv_n(s, 58) > 0) +
                CH_WATER_STICK_CREDIT * (sticks > CH_WATER_STICK_CAP ? CH_WATER_STICK_CAP : sticks);
    }
    /* --provisions: the food held is progress at the kit's rung until the provisions (a hunt's state saved: e42j, easy,
     * 160 meat hunted at obsidian_10 in 5 minutes and none kept); the hunger bar with it, since a hungry player eats
     * the meat as the hunt gets it (e42l: 302 meat got, none of 412 obsidian_10 states holding any) */
    int kit = w->carry_n ? R_OBS20 : R_OBS10;   /* --carry N: the provisions after the N obsidian */
    int pts = w->prov_on && !w->prov && rung == kit ? food_points(s) : 0;
    int bar = w->prov_on && !w->prov && rung == kit ? w->p.food : 0;
    /* --carry N: the obsidian is progress up to N (10 without the flag) */
    int on = w->carry_n > 10 && !w->carry ? w->carry_n : 10;
    return woodp + (iron > 9 ? 9 : iron) + 2 * (dia > 3 ? 3 : dia) + (flint > 1 ? 1 : flint) + (obs > on ? on : obs) +
           ((pts > 40 ? 40 : pts) + bar) / 3;
}

/* a state's kept score: health, food, food held, tools, cobblestone (a dead end starves or has nothing to dig with) */
static float quality(const struct neth_vec_chain_peek *p)
{
    const struct neth_rl_state *s = &p->st;
    static const int FOODS[] = {IT_APPLE, IT_BREAD, IT_PORK, IT_PORK_C, IT_FISH, IT_FISH_C, IT_BEEF, IT_BEEF_C,
                                IT_CHICKEN, IT_CHICKEN_C, IT_CARROT, IT_POTATO_B};
    int food = 0;
    for (size_t k = 0; k < sizeof FOODS / sizeof *FOODS; ++k) food += inv_n(s, FOODS[k]);
    int tier = inv_n(s, IT_DIA_PICK) ? 4 : inv_n(s, IT_IRON_PICK) ? 3 : inv_n(s, IT_STONE_PICK) ? 2 : inv_n(s, IT_WOOD_PICK) ? 1 : 0;
    int sword = inv_n(s, IT_DIA_SWORD) || inv_n(s, IT_IRON_SWORD) || inv_n(s, IT_STONE_SWORD) || inv_n(s, IT_WOOD_SWORD);
    int cob = inv_n(s, IT_COBBLE);
    float q = p->health / 20.0f + 0.5f * (float)p->food / 20.0f + 0.1f * p->saturation / 20.0f;
    /* the food held by its hunger points (a stack of steak is a Nether trip, three apples are not) */
    int pts = food_points(s);
    q += food > 0 ? 0.3f + 0.6f * (float)(pts > 48 ? 48 : pts) / 48.0f : 0.0f;
    q += 0.1f * (float)tier + 0.2f * (float)sword + 0.2f * (float)(cob > 32 ? 32 : cob) / 32.0f;
    if (p->fire > 0) q -= 0.5f;
    return q;
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static uint64_t xs(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static double xu(uint64_t *s) { return (double)(xs(s) >> 11) * 0x1.0p-53; }

/* ------------------------------------------------------------------ nodes */

enum { NS_MAKING, NS_READY, NS_FAILED, NS_FREED };
struct node {
    int id, parent, rung, start, state;
    float q;
    int prog;                   /* prog_of at its save */
    int64_t ticks, seg;        /* the chain's world ticks from the root to this state, this node's segment's */
    int tries, kids, held, pend;    /* segments played from it, nodes saved from them, envs playing (or about to)
                                     * from it, its children's saves being made (they replay from its image) */
    int gain_tries;             /* segments that saved a node over its rung */
    int prov, meat, carry;      /* view.prov, view.meat and view.carry at its save (--provisions, --meat, --carry) */
    int cry;                    /* lane/chain2: the End's crystals the chain destroyed to this node (view.cry_gone at its save) */
    int fed;                    /* lane/chain2 (--endfood): its chain held --endfood's food outside the End */
    int armed;                  /* lane/chain2: its chain wore CH_ARMOR points at its last step outside the End */
    int armw;                   /* ... and the armor points worn at its save (the client's bar) */
    int swords, fresh_swords, held_sword_wear;  /* the End kit's swords and the selected one's durability spent */
    int arm;                    /* lane/chain2: the iron armor worn at its save or an ancestor's (the armor books read 0 after a
                                 * resume: its replay runs no macro, so --kit capped every resumed End state at home) */
    int bad;                    /* --bad: its chain does not replay on the Java client (it or an ancestor listed) */
    int svc;                    /* lane/chain2, the books': the server's crystals left at its save (in the End; -1 else) */
    float dhp;                  /* ... and the dragon's health the view read (-1 none) */
    double wall;
    struct neth_vec_chain_peek p;
};

/* the file of a node's segment: a header, then its policy steps */
#define SEG_MAGIC 0x47455343u   /* "CSEG": the steps raw */
#define SEG_MAGIC2 0x32475343u  /* "CSG2": packed and compressed (write_seg) */
struct seg_head {
    uint32_t magic, step_bytes;
    int32_t id, parent;
    int64_t n;
    struct neth_vec_chain_peek p;   /* the state saved */
};
struct segf {
    struct seg_head h;
    struct rec_step *s;
};
static int chain_load(const char *dir, int node, struct segf **out);
static int seg_read(const char *dir, int id, struct segf *out);
static void to_pool(const struct neth_rl_act *s, struct pool_act *a);
static void act_noop(struct neth_rl_act *a);

/* the run file's keys the search reads (brutetab's grammar) */
struct runf {
    int worlds, groups, macros, world_seed;
    char macro_spec[512], mopts[2048];
    char start_path[2200];
};

static int mopt_add(char *list, size_t n, const char *name, const char *word)
{
    size_t l = strlen(list);
    if (l + (l != 0) + strlen(name) + 1 + strlen(word) + 1 > n) return -1;
    snprintf(list + l, n - l, "%s%s=%s", l ? "," : "", name, word);
    return 0;
}

static void trim(char *s)
{
    char *h = strstr(s, " #");
    if (s[0] == '#') s[0] = 0;
    if (h) *h = 0;
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\n' || s[l - 1] == '\r' || s[l - 1] == ' ')) s[--l] = 0;
}

static int read_run(const char *path, struct runf *r)
{
    FILE *f = fopen(path, "r");
    if (!f) return fprintf(stderr, "chain: --run %s: %s\n", path, strerror(errno)), -1;
    char line[1024], part[64] = "", sec[64] = "";
    while (fgets(line, sizeof line, f))
    {
        trim(line);
        if (!line[0]) continue;
        int ind = 0;
        while (line[ind] == ' ') ++ind;
        char *k = line + ind, *c = strchr(k, ':');
        if (!c) continue;
        *c = 0;
        char *v = c + 1;
        while (*v == ' ') ++v;
        if (ind == 0) { snprintf(part, sizeof part, "%s", k); continue; }
        if (ind == 2 && !*v) { snprintf(sec, sizeof sec, "%s", k); continue; }
        char key[160];
        snprintf(key, sizeof key, "%s.%s.%s", part, sec, k);
        if (!strcmp(key, "sim.pipeline.worlds")) r->worlds = atoi(v);
        else if (!strcmp(key, "sim.pipeline.groups")) r->groups = atoi(v);
        else if (!strcmp(key, "sim.pipeline.start")) snprintf(r->start_path, sizeof r->start_path, "%s", v);
        else if (!strcmp(key, "sim.world.seed")) r->world_seed = atoi(v);
        else if (!strcmp(key, "policy.actions.macros") || (!strcmp(part, "actions") && !strcmp(k, "macros")))
            snprintf(r->macro_spec, sizeof r->macro_spec, "%s", v);
        else if ((!strncmp(key, "policy.actions.", 15) && strcmp(key, "policy.actions.picks")) ||
                 (!strcmp(part, "actions") && strcmp(k, "picks")))
        {
            const char *opt = !strcmp(part, "actions") ? k : key + 15;
            if (mopt_add(r->mopts, sizeof r->mopts, opt, v) < 0)
            {
                fclose(f);
                return fprintf(stderr, "chain: macro options exceed %zu bytes\n", sizeof r->mopts - 1), -1;
            }
        }
    }
    fclose(f);
    return 0;
}

static int macro_bit(const char *name)
{
    int32_t bit;
    for (int i = 0;; ++i)
    {
        const char *n = neth_vec_macro_switch(i, &bit);
        if (!n) return 0;
        if (!strcmp(n, name)) return bit;
    }
}

/* train/loop.c macro_set's grammar (brutetab's copy) */
static int macro_set(const char *spec)
{
    static const char *const BASE = "surface,loot,mine,craft";
    static const char *const RECIPE = "surface,loot,mine,craft,seek,carry,iron,late,liquid,portal,end,fight,table-anywhere";
    char *e;
    long num = strtol(spec, &e, 10);
    if (*spec && !*e) return (int)num;
    int bits = 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        while (*t == ' ') ++t;
        int drop = *t == '-';
        while (*t == '-' || *t == '+') ++t;
        int add = 0;
        if (!strcmp(t, "none") || !*t) add = 0;
        else if (!strcmp(t, "base") || !strcmp(t, "recipe"))
        {
            char l[256];
            snprintf(l, sizeof l, "%s", !strcmp(t, "base") ? BASE : RECIPE);
            for (char *s2, *u = strtok_r(l, ",", &s2); u; u = strtok_r(NULL, ",", &s2)) add |= macro_bit(u);
        }
        else if (!(add = macro_bit(t)))
            return fprintf(stderr, "chain: macros: no such macro %s\n", t), -1;
        bits = drop ? bits & ~add : bits | add;
    }
    return bits;
}

/* ------------------------------------------------------------- the search */

struct env {
    int node, next;             /* the node its segment plays from, the one its next will */
    int64_t last_te;
    int best, bprog;            /* the highest rung its segment began at or saved, and the progress there */
    int64_t gain_at;            /* the decision a rung over best was first seen (-1 none) */
    int gained;                 /* the segment saved a node over its start's rung */
    int cut;                    /* lane/chain2: a gain of this segment's not saved under CH_END_HP health (counted once) */
    int fed;                    /* lane/chain2 (--endfood): the chain held the food outside the End (its node's, or since) */
    int armed;                  /* lane/chain2 (--end-armor): CH_ARMOR points worn at its last step outside the End */
    int ending;                 /* neth_vec_chain_end called: the next reset begins its next segment */
    struct rec_step *log;
    int64_t n, cap;
    uint64_t rng;
    int64_t replay;             /* --resume: the first segment plays a found chain's acts, this many, then is saved */
    struct neth_rl_act *racts;
    int rfrom;                  /* the resumed node's id in --resume's directory */
    int rrung;                  /* its rung there (its chain met the gates below it: --meat, --provisions, --carry) */
    int force_save;             /* the replay done: saved at the next step whatever its rung */
    int resume_saved;           /* --resume-next: the source's first saved node, even while its image is being made */
    int64_t len;                /* this segment's length in steps (S.seg_len, or --dayff's to dusk) */
    int day;                    /* --dayff: this segment is a day's fast-forward */
    struct neth_vec_pearl pearl0; /* the saved controller's counters when this segment began */
    int crystal0;               /* the saved dragon controller's crystal-hit count when this segment began */
    int rollout_started, rollout_done; /* --rollout-out: one uninterrupted attempt from the shared resume image */
    int64_t own[33], kinds[8];  /* --owner-census (lane/legs): the attempt's steps by owning macro bit (32 none), by kind */
    int64_t hand[33];           /* ... (lane/supply, books only) and the steps by owner with the attack held on stone, ore,
                                 * netherrack or obsidian while no pickaxe is in the hotbar and one is in the main inventory */
    /* --target-gain (lane/rods): the kind's count held at the attempt's first step (-1 not read yet), the most held since,
     * and each gain's step, owning macro bit and health (the first CH_GAIN_MAX) */
    int tg0, tg_max;
    int64_t tg_step[CH_GAIN_MAX];
    uint32_t tg_mb[CH_GAIN_MAX];
    float tg_hp[CH_GAIN_MAX];
    /* --build-log (lane/bportal, books only): obsidian held at the first step (-1 not read yet) and at most since; the
     * first step of: a new obsidian held, 10 held, the obsidian macro's want met (OB_KEEP_N, the trace's keep), the frame
     * plan begun (pp_step 0 or past), a whole frame in the look's box, a portal block in it, the Nether (-1 never) */
    int bl_obs0, bl_obsmax, bl_keep;
    int64_t bl_first, bl_10, bl_kept, bl_plan, bl_frame, bl_lit, bl_nether;
    /* --portal-trace: the look's last answer (the most frame cells of one frame in the box, portal blocks in it) */
    int bl_cells, bl_portals;
    /* ... food points held (food.c food_points) at the first step, at bl_kept, bl_frame and bl_nether, and the hunger bar
     * at bl_nether (-1 not reached) */
    int bl_fp0, bl_fpk, bl_fpf, bl_fpn, bl_hn;
    /* ... the food census (neth_vec_food) at bl_kept, so the build log has the provision wait's share (bl_fok 0: not
     * taken) */
    struct neth_vec_food bl_fo;
    int bl_fok;
    /* --nether-trace's rod log (lane/rods, books only): the blaze rods lying within 24 of the feet at the last step */
    int rl_n;
    struct { int32_t id, age; float x, y, z; } rl[32];
    /* --nether-trace's fireball books (lane/rods2): the server's small fireballs within NETH_RL_FB_R at the last step */
    int fb_n;
    int32_t fb_id[NETH_RL_FB_MAX];
    /* lane/proom (--target-gain, --hunt-log): ender pearls and eyes held at the attempt's first step (-1 not read yet),
     * the most held since, and each new count's step, day time and owning macro bit (k: start + k + 1 held) */
    int hunt0, hunt_max;
    int64_t gain_step[12], gain_wt[12];
    uint32_t gain_mb[12];
    /* lane/proom (--death-trace): the attempt's last DT_RING steps (a ring, dumped at its death), and (--hunt-log) its
     * night steps under the pearl macro and its steps with an enderman in the macro's view */
    struct dt_row *dt;
    int64_t dt_n;
    int kt_ek, kt_set;          /* lane/pground (--kill-trace): the stats' endermen killed at the last step, read once */
    struct lv_row *lv;          /* lane/lava (--lava-log): the attempt's last LV_RING steps (a ring, dumped at its death) */
    int64_t lv_n;
    int64_t hunt_night, ender_view;
    /* lane/proom (--hunt-log, PRIVILEGED census, read only): the overworld's night steps alive, the endermen alive within
     * 64 and within 128 blocks (horizontally; the census's 16 nearest) summed over them, the stats screen's endermen
     * killed at the attempt's first step (-1 before) */
    int64_t night_steps, alive64, alive128;
    int32_t ek0;
    /* lane/pnight (--night-log, read only): the night under way (1 the first; a night ends at the day clock's 23000,
     * dawn), its dawn index at the attempt's first step (-1 not read yet), and this night's counts so far */
    int64_t nl_day0;
    int nl_k;
    struct nl_acc *nl;
    /* lane/dleg (--ladder-log, books only): the first step each End rung held (LAD_RUNGS, -1 never), the books at the
     * attempt's first step in the End (ld_ok 0: none yet; LAD_BOOKS), the lowest health and dragon health seen */
    int64_t ld_at[10];
    int ld_ok;
    float ld0[11];
    float ld_hpmin, ld_dmin;
    /* ... the server's crystals by their pillar (the crystal rungs above are the macro's own hit books: a client copy
     * gone in an arrow's flight, lobs twice): each crystal seen standing in the server's list (its cell), and gone once
     * its cell's chunk is in the client world (the server's then too) with the full list (sv_ncry within its 16)
     * not holding it; seen, gone, the step of the last gone (-1 none), gone at the dragon_75 rung (-1 not reached) */
#define LAD_CRY 48
    float lc_p[LAD_CRY][3];
    unsigned char lc_gone[LAD_CRY];
    int lc_n, lc_ngone, lc_g75;
    int64_t lc_last;
    /* lane/dleg (--end-trace-ender's void_d): the last End trace row's health, wing pushes, feet height and block
     * under the feet (an event row: health fell, a push, or the feet rose 0.3 off a floor) */
    float vd_hp;
    double vd_y;
    int vd_push, vd_under, vd_ok;
    struct dg_acc *dg;          /* lane/pdig (--dig-log, read only) */
    int sp_entry;               /* lane/supply (--supply-log, books only): the Nether entry's row written, */
    int64_t sp_arm;             /* ... the steps the armour macro drove (neth_vec_kit's on), the overworld's, */
    char *sp_alive;             /* ... and the last living step's row (supply_fmt) */
    int sp_rinit;               /* ... and the armour ladder's first steps (supply_rungs), set to -1 at the first step */
    int64_t sp_rung[SPR_N];
    struct sg_acc *sg;          /* lane/pdig (--sight-log, read only) */
    struct fl_acc *fl;          /* lane/pground (--floor-log, read only) */
    int64_t fw_at[40];          /* lane/fwd (--stage-log): each fw_ev's first step (-1 not yet) */
    int fw_mem;                 /* lane/fwd (--replay-mem): the restore's memory read */
    int64_t ch_seen;            /* lane/choice (--choice-log): the armour slot's conflicts already written */
    int ch_dumped;              /* lane/choice (--mem-at): the memory written */
    struct wl_acc *wl;          /* lane/wood (--wood-log, books only) */
};

/* --wood-log FILE (lane/wood, books only; nothing here reaches a macro): the first log's story of an attempt. At the
 * attempt's first step a PRIVILEGED scan of the client world's blocks round the eye (WL_SCAN_R across, WL_SCAN_DY up and
 * down): the nearest log of each block (17, 162) and the logs within WL_NEAR_R, those with a clear line from the eye (no
 * block but air, tall grass, flowers, snow or leaves' own log on it: the cell's centre seen from the eye in some
 * direction). Then the first step each held: a log (17) in the cast's seen record (the observation's own view, what a
 * player saw; 162 is not in it), a log under the crosshair (mo_block 17 or 162: targeted), stat.mineBlock of either log
 * over 0 (harvested), a log in the inventory (held), a wooden pickaxe held; the most horizontal distance from the spawn
 * and the lowest and highest feet; the steps with the eye in water and with no free slot; a row every WL_EVERY steps. */
#define WL_SCAN_R 32
#define WL_SCAN_DY 24
#define WL_NEAR_R 24.0
#define WL_EVERY 20000
struct wl_acc {
    int init;
    double x0, y0, z0, maxd, ymin, ymax;
    int64_t seen17, aim, mined, held, wpick, water, full, roofed;   /* roofed: PRIVILEGED, sampled every 10th step */
    float seen_d;
    int64_t own[24];            /* the steps a macro kind (peek.kind, M_*: 0 none) owned */
    /* the steps labelled the surface's (mbit NETH_VEC_SURFACE) by the dispatch branch that made them (neth_vec_lsrc):
     * [0] the air rescue (VLS_AIR), [1] food_provision's surface stage, [2] the swim macro (kind M_SURFACE), [3] no
     * macro with the feet's cell water (PRIVILEGED: water_exit's), [4] no macro and dry (inwall's dig), [5] other */
    int64_t surf[6];
    int64_t rescue_runs, rescue_long; /* air rescue runs begun, and those over WL_RESCUE_LONG steps */
    int64_t rescue_t0, rescue_max;
    int in_rescue;
};
#define WL_RESCUE_LONG 1000
static int wl_clear_block(int b)
{
    return b == 0 || b == 31 || b == 32 || b == 37 || b == 38 || b == 78 || b == 106 || b == 175;
}
static void wl_scan(const struct neth_vec *v, int i, const struct neth_vec_chain_peek *p, FILE *f)
{
    double ex = p->x, ey = p->y + 1.62, ez = p->z;
    int fx = (int)floor(ex), fy = (int)floor(ey), fz = (int)floor(ez);
    double near[2] = {-1, -1};
    int n24[2] = {0, 0}, clear24[2] = {0, 0}, loaded = 0;
    double clear_d = -1;
    for (int dx = -WL_SCAN_R; dx <= WL_SCAN_R; ++dx)
        for (int dz = -WL_SCAN_R; dz <= WL_SCAN_R; ++dz)
            for (int dy = -WL_SCAN_DY; dy <= WL_SCAN_DY; ++dy)
            {
                int x = fx + dx, y = fy + dy, z = fz + dz;
                if (y < 0 || y > 255) continue;
                int b = neth_vec_nether_block(v, i, x, y, z);
                if (b >= 0) ++loaded;
                if (b != 17 && b != 162) continue;
                int q = b == 162;
                double cx = x + 0.5 - ex, cy = y + 0.5 - ey, cz = z + 0.5 - ez, d = sqrt(cx * cx + cy * cy + cz * cz);
                if (near[q] < 0 || d < near[q]) near[q] = d;
                if (d > WL_NEAR_R) continue;
                ++n24[q];
                /* the line from the eye to the cell's centre, every 0.05 block, clear of every other block */
                int ok = 1, steps = (int)(d / 0.05);
                for (int s = 1; s < steps && ok; ++s)
                {
                    double t = (double)s / steps;
                    int bx = (int)floor(ex + cx * t), by = (int)floor(ey + cy * t), bz = (int)floor(ez + cz * t);
                    if (bx == x && by == y && bz == z) break;
                    ok = wl_clear_block(neth_vec_nether_block(v, i, bx, by, bz));
                }
                if (ok)
                {
                    ++clear24[q];
                    if (clear_d < 0 || d < clear_d) clear_d = d;
                }
            }
    fprintf(f, "S\t%d\t%.3f\t%.3f\t%.3f\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%.2f\t%d\n", i, p->x, p->y, p->z, near[0], near[1],
            n24[0], n24[1], clear24[0], clear24[1], clear_d, loaded);
}

/* --floor-log (lane/pground, orders 1:49 PM, the prebuilt floor screen; read only, PRIVILEGED: neth_rl_floor, the server's
 * livings and light): a row an attempt a half day (day: the day's time 23000 to 13000, night 13000 to 23000) and at
 * the attempt's end, over the box --floor-box names: the distinct monsters with the feet inside, by id, the endermen
 * apart, each counted born when first seen inside FL_BORN ticks old or younger; the monster-steps inside (a sample
 * every FL_EVERY steps, times FL_EVERY); the spawner's countEntities and cap at the samples (summed); the client's
 * endermen within 4 of the eye (neth_rl_far's near: distinct contacts) and those screaming in view (provoked); the stats'
 * endermen killed; pearls and eyes held at the period's start and end; the steps with the feet within 1 of the first
 * step's place (home); the box's most sky and block light saved, its air cells and spawnable cells at the first
 * sample of the period */
#define FL_EVERY 5
#define FL_BORN 20
#define FL_IDS 256
struct fl_acc {
    int set, per, night, held0, ek0, sky_max, blk_max, cells, spawnable, lit;
    int64_t steps, home, mon_steps, samples, capn, cap, ender_steps;
    double hx, hz;
    int nid, id[FL_IDS];
    unsigned char ender[FL_IDS], born[FL_IDS];
    int ncon, con[FL_IDS], nprov, prov[FL_IDS];
    /* orders 2:28 PM (what holds the cap): the samples' sums of the overworld's monsters by kind and ticking, by chunk
     * band and ticking; the monsters that left the list between two samples by their last ticking flag (a frozen one
     * leaves only with its chunk or by a kill: it never despawns while frozen), and those that came */
    int64_t ck[NETH_RL_FLOOR_K][2], cb[4][2], left[2], came;
    int primed, pn, pid[NETH_RL_FLOOR_M];   /* a sample taken; the last sample's list (both kept across periods) */
    unsigned char ptick[NETH_RL_FLOOR_M];
    /* orders 2:38 PM: the spawner's monster cap test (neth_rl_floor's gate: tests, open, walks, made), the period's
     * delta from the last sample before it to its own last sample (FL_EVERY's grain at the edges); glast kept
     * across periods */
    int gprimed;
    int64_t gbase[4], glast[4];
};

static void fl_crow(FILE *f, int i, const struct fl_acc *g)
{
    fprintf(f, "C\t%d\t%d\t%s\t%lld\t", i, g->per, g->night ? "night" : "day", (long long)g->samples);
    for (int k = 0; k < NETH_RL_FLOOR_K; ++k) fprintf(f, "%s%lld:%lld", k ? "," : "", (long long)g->ck[k][1], (long long)g->ck[k][0]);
    fputc('\t', f);
    for (int k = 0; k < 4; ++k) fprintf(f, "%s%lld:%lld", k ? "," : "", (long long)g->cb[k][1], (long long)g->cb[k][0]);
    fprintf(f, "\t%lld\t%lld\t%lld", (long long)g->left[1], (long long)g->left[0], (long long)g->came);
    for (int k = 0; k < 4; ++k) fprintf(f, "\t%lld", (long long)(g->glast[k] - g->gbase[k]));
    fputc('\n', f);
}

static int fl_has(const int *ids, int n, int id)
{
    for (int k = 0; k < n; ++k)
        if (ids[k] == id) return 1;
    return 0;
}

static void fl_row(FILE *f, int i, const struct fl_acc *g, const char *why, int held, int ek)
{
    int eb = 0, ei = 0, ob = 0, oi = 0;
    for (int k = 0; k < g->nid; ++k)
    {
        if (g->ender[k]) ++ei, eb += g->born[k];
        else ++oi, ob += g->born[k];
    }
    fprintf(f, "F\t%d\t%d\t%s\t%s\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\n",
            i, g->per, g->night ? "night" : "day", why, (long long)g->steps, g->held0, held, ek >= 0 && g->ek0 >= 0 ? ek - g->ek0 : -1,
            eb, ei, ob, oi, (long long)(g->mon_steps), (long long)g->ender_steps, (long long)g->samples,
            (long long)g->capn, (long long)g->cap, g->ncon, g->nprov, (long long)g->home, g->sky_max, g->blk_max, g->cells,
            g->spawnable);
}

static void fl_begin(struct fl_acc *g, int night, int held, int ek)
{
    int per = g->set ? g->per + 1 : 0;
    double hx = g->hx, hz = g->hz;
    int set = g->set, pn = g->pn, primed = g->primed, gprimed = g->gprimed;
    int64_t glast[4];
    memcpy(glast, g->glast, sizeof glast);
    int *pid = malloc(sizeof g->pid);
    unsigned char *ptick = malloc(sizeof g->ptick);
    if (pid && ptick) memcpy(pid, g->pid, sizeof g->pid), memcpy(ptick, g->ptick, sizeof g->ptick);
    memset(g, 0, sizeof *g);
    g->set = 1, g->per = per, g->night = night, g->held0 = held, g->ek0 = ek;
    if (set) g->hx = hx, g->hz = hz;
    g->primed = primed && pid && ptick;
    if (pid && ptick) g->pn = pn, memcpy(g->pid, pid, sizeof g->pid), memcpy(g->ptick, ptick, sizeof g->ptick);
    free(pid), free(ptick);
    g->gprimed = gprimed;
    memcpy(g->gbase, glast, sizeof glast), memcpy(g->glast, glast, sizeof glast);
}

/* lane/fwd (--stage-log): the events an attempt's first hit is logged for: stages.h's chain past spawn (1 .. STAGE_N-1),
 * then these */
enum { FE_STAGES = STAGE_N - 1, FE_ROD7 = FE_STAGES, FE_PEARL12, FE_EYE12, FE_CRY10, FE_N };
_Static_assert(FE_N <= 40, "fw_at");
static void fw_name(int k, char *out, size_t n)
{
    static const char *const extra[] = {"rods_7", "pearls_12", "eyes_12", "crystals_gone10"};
    snprintf(out, n, "%s", k < FE_STAGES ? STAGE_CHAIN[k + 1].name : extra[k - FE_STAGES]);
}

/* --ladder-log (lane/dleg): the End's rungs, stages.h's names (crystals_goneN: the macro's own hit books, not the
 * server's; the log carries the server's crystals by pillar beside them: cry_seen, cry_gone, cry_last, cry_gone75) */
#define LAD_RUNGS 10
static const char *const LAD_NAME[LAD_RUNGS] = {"crystals_gone1", "crystals_gone3", "crystals_gone6", "crystals_gone9",
                                                "crystals_gone10", "dragon_75", "dragon_50", "dragon_25", "dragon_dead",
                                                "exit"};

/* --dig-log (lane/pdig; PRIVILEGED, neth_rl_dig, a trace's only): the digging ceiling. A visit is one enderman (by entity
 * id) within NETH_RL_DIG_R blocks of the feet (three dimensions), ended after DG_GAP steps out of it; its row: the first and
 * last steps within 48, 32 and 16, its steps within each by day and night (the day clock's 13000 to 23000 night), the
 * least distance and the height of its feet over the player's then, and at its first step within 32 (and 48) the
 * offset and the line's cells by kind (neth_rl_dig). The attempt's row: its living overworld steps by day and night, the
 * enderman-steps within 16, 32, 48, those within 32 by height (feet against feet: under -16, -16 to -6, -6 to 6, over
 * 6), with lava, water or loose on the line, the strip's books (cells walked into, steps it drove, cobblestone got) */
#define DG_V 48
#define DG_GAP 100
struct dg_visit {
    int32_t id;
    int64_t f[3], l[3], st[3][2], wt0;      /* band 0: 48, 1: 32, 2: 16 (first, last step; -1 none) */
    float dmin, dydmin, e[2][3];
    int16_t c[2][NETH_RL_DIG_K];
    int8_t sky[2];
};
struct dg_acc {
    int nv;
    struct dg_visit v[DG_V];
    int64_t steps[2], es[3][2], dyb[4][2], line[3][2];
    /* the N rows (orders 7:06 AM, the ground covered): the period from one dawn (the day clock's 23000) to the next as
     * --night-log's nights (k 1 the first), and in its night steps (13000 to 23000, alive, overworld): the steps with
     * the feet under the open sky, moving (0.1 blocks or more across since the last step), the blocks travelled
     * across, the distinct endermen within 64 and 128 (3D) under the sky and any (ids; DG_IDS at most a set) */
    int64_t day0;
    int nk, has_last;
    double lx, lz, dist;
    int64_t nsteps, nsky, nmove;
    int nid[4];
    int32_t ids[4][64];
    /* the W rows (orders 7:46 AM, P1: the standing still's cause): the period's night steps (as N's) still (0: under
     * 0.1 blocks across since the last step) and moving (1), by the owning macro bit (32 none), by the pearl phase
     * (PP_*, 12 the hunt off), by the pearl book's PG_WHY (the step's rise) and neth_vec_pearl_gate's split of its
     * policy steps; the oracle walk's books' rises (nl_ob's nine) on still steps (0) and on all (1) */
    int wset;
    int32_t why_l[12], gate_l[3], ob_l[9];
    int64_t w_own[33][2], w_ph[13][2], w_why[12][2], w_gate[3][2], w_ob[9][2];
    int64_t w_src[24][2];       /* by the dispatch's branch (neth_vec_lsrc: VLS_*, 8 * provision's stage + VLP_*) */
    int64_t w_wood[16][2];      /* by provision's wood step's branch ((neth_vec_lsrc >> 8) & 15: VLW_*, + 8 craft) */
    int64_t w_idle[32][2];      /* provision standing idle by why the strip did not start ((>> 16) & 31: macros.c
                                 * provision_idle_why, + 16 the feet at y 62 or over) */
    int64_t w_pvx[24][16][2];
    /* the D rows (orders 10:48 AM, F1: the food by day): the period's day steps (the day clock's 23000 to 13000, alive,
     * overworld): food level and food points held at its first day step (dawn) and at its last (dusk), raw and cooked
     * meat held there; the day steps by food level band (14 and over, 7 to 13, under 7) and the dispatch's branch
     * (neth_vec_lsrc & 255) and by owning bit; the food books' rises (neth_vec_food: eatings begun and done, hunts and
     * their meat, meat and other food picked up, other eaten, scouts and those that found, the scouts' ends by why, the
     * hunts' ends by why, trips and their meat); the census's food animals (PRIVILEGED: pigs, cows, mooshrooms, chickens
     * within NETH_RL_CENSUS_R, a trace's only): the summed count, the steps with one, the nearest's distance band */
    int dset, dseen;
    int32_t d_f0, d_fp0, d_raw0, d_ck0, d_f1, d_fp1, d_raw1, d_ck1;
    int64_t d_steps, d_bs[3][24], d_own[33], d_an, d_any, d_near[4];
    int64_t d_pv[10][2][2];     /* provision's day steps by the line that decided its stage (PVW_*), the feet at y 62 or
                                 * over (1), under the open sky (1) */
    int64_t d_sc[6][5];         /* (F2c) the scouts ended in the period by why (scout_end's order): count, the farthest
                                 * each reached from its start summed (scout_far's rise), those with a food animal in the
                                 * census at the end, the nearest's distance then summed, its height under the feet summed */
    int64_t d_fmv[2];           /* (F2c) day steps owned by the food bit: standing (moved under 0.1), moving */
    /* the K rows (orders 12:46 PM, the walks' trace; counting only): the period's steps by context (0 the food bit
     * owns, 1 provision's wood step, 2 the pearl bit owns, 3 any other), night (1), the step's last walk primitive call
     * (neth_vec_walk's code, 0 none) and the move across that step (under 0.01, 0.01 to 0.1, 0.1 and over); on the
     * steps under 0.01: the target's distance band (under 16, 32, 48, 64, 64 and over), the summed height over the feet
     * and the summed way length */
    int64_t k_n[4][2][64][3], k_d[4][2][64][5], k_dy[4][2][64], k_len[4][2][64];
    int64_t k_f[4][2][64][11];  /* ... and on its steps under 0.1 the cell ahead (neth_vec_walk's out[4], 0 not read) */
    int64_t k_c[4][2][3][128];  /* ... sho_follow walking, walking sneaking and the spiral's leg on steps under 0.1: by
                                 * neth_vec_walk's out[5] (the way's next cell; the spiral's water and ground) */
    struct neth_vec_food d_fb0, d_fbl;   /* provision's steps by branch and the line that put it in its stage (>> 12: PVW_*) */
    /* the G rows (lane/pground, item 1: the ground a night was spent on; PRIVILEGED, neth_rl_ground, a trace's only): the
     * period's night steps (as N's) read; every GR_EVERY-th of them the columns within NETH_RL_GROUND_R by kind (open,
     * water, canopy, none) summed; the feet's y summed and the steps under y 62; the steps by the feet's biome; the
     * distinct endermen within 64 (3D, feet to feet) with a clear line from the eye at some step, and those clear and
     * under the sky at that step (ids; DG_IDS at most a set) */
    int64_t gr_n, gr_cn, gr_c[NETH_RL_GR_K], gr_low, gr_bio[256];
    double gr_y;
    int gr_nid[2];
    int32_t gr_ids[2][64];
    /* the E rows (lane/pground, the orders of 7:37 PM: the supply's own accounting; PRIVILEGED, neth_rl_ground): every
     * enderman within 128 of the feet (3D) at some night step of the period, by id (GR_TR at most; over: gr_trover):
     * its first and last step within 128 (the period's night steps, counted from 0), its ticks existed when first
     * within 128 (under GR_BORN: born there, else arrived), within 64 at some step, a clear line within the frame's
     * reach (neth_rl_ground's reach) at some step, and within 64 too; how it left: still within 128 at the period's end,
     * alive beyond 128 then (out of range), gone from the world's list last seen dying (died) or alive (removed:
     * despawned or its chunk unloaded); the feet's place at the first and the last night step (net distance) */
    int gr_ntr, gr_trover, gr_pos;
    double gr_x0, gr_z0, gr_x1, gr_z1;
    int64_t gr_trunc;
    struct gr_tr { int32_t id, age0, age1; int64_t first, last; int8_t in64, reach, reach64, dead, out, gone;
                   int64_t first64, last64; int32_t s64[4], c64[4]; int8_t cell; } gr_tr[128];
    /* the Q rows (orders 8:10 PM, the 2x2): the period's night enderman-steps within 64 (alive) by cell (2 x the
     * player's feet under the sky + the enderman's): steps, with a clear line, with a line in reach; and the P rows:
     * the feet's cell every GR_POS_EVERY night steps (GR_POS at most) */
    int64_t q_st[4], q_cl[4], q_rc[4];
    int gr_np;
    int32_t gr_p[24][3];
    /* the O rows (orders 9:29 PM, the caves' openness): the period's night steps with the feet not under the sky,
     * every GR_EVERY-th (the G rows' sample), by band of neth_rl_ground's air8 (OP_BAND's bounds): samples, the endermen
     * alive within 64 then (enderman-samples), those with a clear line, the distinct ids with a clear line (OP_IDS a
     * band at most), and the samples by owning macro bit (32 none) and by pearl phase (12 the hunt off) */
    int64_t o_n[4], o_es[4], o_ec[4], o_own[4][33], o_ph[4][13];
    int o_nid[4];
    int32_t o_ids[4][32];
    /* the A rows (orders 10:01 PM, item A: what the render distance leaves the bot): the period's night steps, every
     * GR_EVERY-th (any sky): samples; summed: the spawner's monster count and its cap, the loaded chunks within
     * NETH_RL_GROUND_LR, those inside the watched square, the farthest; the monster livings and the endermen by
     * [ticking][inside the watched square]; the endermen alive within 64 by the same; the clear ones under the sky within
     * 64 inside and outside the square; the most the count reached; distinct ids clear within 64 inside and outside */
    int64_t a_n, a_capn, a_cap, a_load, a_loadw, a_lmax, a_mon[2][2], a_end[2][2], a_e64[2][2], a_cs[2];
    int a_capmax, a_view, a_nid[2];
    int32_t a_ids[2][32];
};
#define OP_IDS 32
static int op_band(int air8) { return air8 < 32 ? 0 : air8 < 128 ? 1 : air8 < 512 ? 2 : 3; }
#define GR_POS_EVERY 500
#define GR_POS 24
#define GR_EVERY 20
#define GR_TR 128
#define GR_BORN 40
#define DG_IDS 64
static void dg_drow(FILE *f, int i, const struct dg_acc *g, const char *why);
static void dg_nrow(FILE *f, int i, const struct dg_acc *g, const char *why)
{
    fprintf(f, "N\t%d\t%d\t%s\t%lld\t%lld\t%lld\t%.1f\t%d\t%d\t%d\t%d\n", i, g->nk, why, (long long)g->nsteps,
            (long long)g->nsky, (long long)g->nmove, g->dist, g->nid[0], g->nid[1], g->nid[2], g->nid[3]);
    /* the G row (lane/pground): night steps read, column samples, open, water, canopy, none (summed), the feet's mean
     * y, steps under y 62, distinct clear within 64, clear and under the sky, then biome:steps by steps */
    fprintf(f, "G\t%d\t%d\t%s\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%.2f\t%lld\t%d\t%d\t", i, g->nk, why,
            (long long)g->gr_n, (long long)g->gr_cn, (long long)g->gr_c[NETH_RL_GR_OPEN], (long long)g->gr_c[NETH_RL_GR_WATER],
            (long long)g->gr_c[NETH_RL_GR_CANOPY], (long long)g->gr_c[NETH_RL_GR_NONE], g->gr_n ? g->gr_y / (double)g->gr_n : -1.0,
            (long long)g->gr_low, g->gr_nid[0], g->gr_nid[1]);
    {
        int any = 0, used[256] = {0};
        for (;;)
        {
            int bk = -1;
            for (int k = 0; k < 256; ++k)
                if (!used[k] && g->gr_bio[k] && (bk < 0 || g->gr_bio[k] > g->gr_bio[bk])) bk = k;
            if (bk < 0) break;
            used[bk] = 1;
            fprintf(f, "%s%d:%lld", any++ ? "," : "", bk, (long long)g->gr_bio[bk]);
        }
        fputs(any ? "\n" : "-\n", f);
    }
    {
        /* the E row (lane/pground): within 128, within 64, born, arrived; left by: died, removed, out of range, still
         * there; their steps within 128 (first to last) summed; the removed ones' ticks existed at the last sight
         * summed; with a line in reach, in reach and within 64; path, net distance; trackers over GR_TR; steps whose
         * list was cut at NETH_RL_GROUND_E */
        int n64 = 0, born = 0, died = 0, rem = 0, out = 0, still = 0, rc = 0, rc64 = 0;
        int64_t span = 0, age_rem = 0;
        for (int k = 0; k < g->gr_ntr; ++k)
        {
            const struct gr_tr *t = &g->gr_tr[k];
            n64 += t->in64, born += t->age0 < GR_BORN, rc += t->reach, rc64 += t->reach64;
            span += t->last - t->first + 1;
            if (t->gone && t->dead) ++died;
            else if (t->gone) ++rem, age_rem += t->age1;
            else if (t->out) ++out;
            else ++still;
        }
        fprintf(f, "E\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%lld\t%d\t%d\t%.1f\t%.1f\t%d\t%lld\n", i, g->nk, why,
                g->gr_ntr, n64, born, g->gr_ntr - born, died, rem, out, still, (long long)span, (long long)age_rem, rc,
                rc64, g->dist, g->gr_pos ? hypot(g->gr_x1 - g->gr_x0, g->gr_z1 - g->gr_z0) : 0.0, g->gr_trover,
                (long long)g->gr_trunc);
        /* the Q row: by cell (0 player under, ender under; 1 under, ender sky; 2 sky, under; 3 sky, sky): enderman-
         * steps, clear, in reach; distinct with a clear step in the cell; died with the cell its last step within 64
         * had; then the endermen ever within 64: count, their steps from the first to the last step within 64
         * summed, and how they left (died, removed, out of range, still there) */
        fprintf(f, "Q\t%d\t%d\t%s", i, g->nk, why);
        int in = 0, ld = 0, lr = 0, lo = 0, ls = 0;
        int64_t life = 0;
        for (int c = 0; c < 4; ++c)
        {
            int dc = 0, dd = 0;
            for (int k = 0; k < g->gr_ntr; ++k)
            {
                const struct gr_tr *t = &g->gr_tr[k];
                dc += t->c64[c] > 0;
                dd += t->in64 && t->gone && t->dead && t->cell == c;
            }
            fprintf(f, "\t%lld,%lld,%lld,%d,%d", (long long)g->q_st[c], (long long)g->q_cl[c], (long long)g->q_rc[c], dc, dd);
        }
        for (int k = 0; k < g->gr_ntr; ++k)
        {
            const struct gr_tr *t = &g->gr_tr[k];
            if (!t->in64) continue;
            ++in, life += t->last64 - t->first64 + 1;
            if (t->gone && t->dead) ++ld;
            else if (t->gone) ++lr;
            else if (t->out) ++lo;
            else ++ls;
        }
        fprintf(f, "\t%d,%lld,%d,%d,%d,%d\n", in, (long long)life, ld, lr, lo, ls);
        fprintf(f, "P\t%d\t%d\t%s\t", i, g->nk, why);
        for (int k = 0; k < g->gr_np; ++k)
            fprintf(f, "%s%d,%d,%d", k ? ";" : "", g->gr_p[k][0], g->gr_p[k][1], g->gr_p[k][2]);
        fputs(g->gr_np ? "\n" : "-\n", f);
        /* the O row: per band samples,enderman-samples,clear,distinct clear|owner bit:samples,...|phase:samples,... */
        fprintf(f, "O\t%d\t%d\t%s", i, g->nk, why);
        for (int c = 0; c < 4; ++c)
        {
            fprintf(f, "\t%lld,%lld,%lld,%d|", (long long)g->o_n[c], (long long)g->o_es[c], (long long)g->o_ec[c],
                    g->o_nid[c]);
            int any = 0;
            for (int k = 0; k < 33; ++k)
                if (g->o_own[c][k]) fprintf(f, "%s%d:%lld", any++ ? "," : "", k, (long long)g->o_own[c][k]);
            fputs(any ? "|" : "-|", f);
            any = 0;
            for (int k = 0; k < 13; ++k)
                if (g->o_ph[c][k]) fprintf(f, "%s%d:%lld", any++ ? "," : "", k, (long long)g->o_ph[c][k]);
            if (!any) fputc('-', f);
        }
        fputc('\n', f);
        /* the R row (the A sums; R: the A tag is the attempt's row): samples view capn cap capmax load loadw lmax (sums)
         * mon end e64 ([tick][watch] 00,01,10,11) cs_in,cs_out ids_in,ids_out */
        fprintf(f, "R\t%d\t%d\t%s\t%lld\t%d\t%lld\t%lld\t%d\t%lld\t%lld\t%lld", i, g->nk, why, (long long)g->a_n, g->a_view,
                (long long)g->a_capn, (long long)g->a_cap, g->a_capmax, (long long)g->a_load, (long long)g->a_loadw,
                (long long)g->a_lmax);
        const int64_t (*ar[3])[2] = {g->a_mon, g->a_end, g->a_e64};
        for (int r = 0; r < 3; ++r)
            fprintf(f, "\t%lld,%lld,%lld,%lld", (long long)ar[r][0][0], (long long)ar[r][0][1], (long long)ar[r][1][0],
                    (long long)ar[r][1][1]);
        fprintf(f, "\t%lld,%lld\t%d,%d\n", (long long)g->a_cs[1], (long long)g->a_cs[0], g->a_nid[1], g->a_nid[0]);
    }
    /* the W row: each list still/moving pairs, the owners bit:still:moving where any */
    fprintf(f, "W\t%d\t%d\t%s\t", i, g->nk, why);
    int any = 0;
    for (int k = 0; k < 33; ++k)
        if (g->w_own[k][0] || g->w_own[k][1])
            fprintf(f, "%s%d:%lld:%lld", any++ ? "," : "", k, (long long)g->w_own[k][0], (long long)g->w_own[k][1]);
    fputs(any ? "\t" : "-\t", f);
    for (int k = 0; k < 13; ++k)
        fprintf(f, "%s%lld/%lld", k ? "," : "", (long long)g->w_ph[k][0], (long long)g->w_ph[k][1]);
    fputc('\t', f);
    for (int k = 0; k < 12; ++k)
        fprintf(f, "%s%lld/%lld", k ? "," : "", (long long)g->w_why[k][0], (long long)g->w_why[k][1]);
    fputc('\t', f);
    for (int k = 0; k < 3; ++k)
        fprintf(f, "%s%lld/%lld", k ? "," : "", (long long)g->w_gate[k][0], (long long)g->w_gate[k][1]);
    fputc('\t', f);
    for (int k = 0; k < 9; ++k)
        fprintf(f, "%s%lld/%lld", k ? "," : "", (long long)g->w_ob[k][0], (long long)g->w_ob[k][1]);
    fputc('\t', f);
    any = 0;
    for (int k = 0; k < 24; ++k)
        if (g->w_src[k][0] || g->w_src[k][1])
            fprintf(f, "%s%d:%lld:%lld", any++ ? "," : "", k, (long long)g->w_src[k][0], (long long)g->w_src[k][1]);
    fputs(any ? "\t" : "-\t", f);
    any = 0;
    for (int k = 0; k < 16; ++k)
        if (g->w_wood[k][0] || g->w_wood[k][1])
            fprintf(f, "%s%d:%lld:%lld", any++ ? "," : "", k, (long long)g->w_wood[k][0], (long long)g->w_wood[k][1]);
    fputs(any ? "\t" : "-\t", f);
    any = 0;
    for (int k = 8; k < 24; ++k)
        for (int q = 0; q < 16; ++q)
            if (g->w_pvx[k][q][0] || g->w_pvx[k][q][1])
                fprintf(f, "%s%d.%d:%lld:%lld", any++ ? "," : "", k, q, (long long)g->w_pvx[k][q][0],
                        (long long)g->w_pvx[k][q][1]);
    fputs(any ? "\t" : "-\t", f);
    any = 0;
    for (int k = 0; k < 32; ++k)
        if (g->w_idle[k][0] || g->w_idle[k][1])
            fprintf(f, "%s%d:%lld:%lld", any++ ? "," : "", k, (long long)g->w_idle[k][0], (long long)g->w_idle[k][1]);
    fputs(any ? "\n" : "-\n", f);
    dg_drow(f, i, g, why);
}
static void dg_drow(FILE *f, int i, const struct dg_acc *g, const char *why)
{
    fprintf(f, "D\t%d\t%d\t%s\t%d,%d,%d,%d\t%d,%d,%d,%d\t%lld\t", i, g->nk, why, g->d_f0, g->d_fp0, g->d_raw0, g->d_ck0,
            g->d_f1, g->d_fp1, g->d_raw1, g->d_ck1, (long long)g->d_steps);
    int any = 0;
    for (int b = 0; b < 3; ++b)
        for (int k = 0; k < 24; ++k)
            if (g->d_bs[b][k]) fprintf(f, "%s%d.%d:%lld", any++ ? "," : "", b, k, (long long)g->d_bs[b][k]);
    fputs(any ? "\t" : "-\t", f);
    any = 0;
    for (int k = 0; k < 33; ++k)
        if (g->d_own[k]) fprintf(f, "%s%d:%lld", any++ ? "," : "", k, (long long)g->d_own[k]);
    fputs(any ? "\t" : "-\t", f);
    const struct neth_vec_food *a = &g->d_fb0, *z = &g->d_fbl;
    fprintf(f, "%d,%d,%d,%d,%d,%d,%d,%d,%d", z->eat_starts - a->eat_starts, z->eats - a->eats, z->hunts - a->hunts,
            z->hunt_meat - a->hunt_meat, z->meat_got - a->meat_got, z->other_got - a->other_got,
            z->other_eaten - a->other_eaten, z->scouts - a->scouts, z->scout_found - a->scout_found);
    fputc('\t', f);
    for (int k = 0; k < 6; ++k) fprintf(f, "%s%d", k ? "," : "", z->scout_end[k] - a->scout_end[k]);
    fputc('\t', f);
    for (int k = 0; k < 4; ++k) fprintf(f, "%s%d", k ? "," : "", z->hunt_end[k] - a->hunt_end[k]);
    fprintf(f, "\t%d,%d\t%lld,%lld,%lld,%lld,%lld,%lld\t", z->trips - a->trips, z->trip_meat - a->trip_meat,
            (long long)g->d_an, (long long)g->d_any, (long long)g->d_near[0], (long long)g->d_near[1],
            (long long)g->d_near[2], (long long)g->d_near[3]);
    any = 0;
    for (int k = 0; k < 10; ++k)
        for (int h = 0; h < 2; ++h)
            for (int y = 0; y < 2; ++y)
                if (g->d_pv[k][h][y]) fprintf(f, "%s%d.%d.%d:%lld", any++ ? "," : "", k, h, y, (long long)g->d_pv[k][h][y]);
    fputs(any ? "\t" : "-\t", f);
    for (int k = 0; k < 6; ++k)
        fprintf(f, "%s%lld:%lld:%lld:%lld:%lld", k ? "," : "", (long long)g->d_sc[k][0], (long long)g->d_sc[k][1],
                (long long)g->d_sc[k][2], (long long)g->d_sc[k][3], (long long)g->d_sc[k][4]);
    fprintf(f, "\t%lld,%lld\n", (long long)g->d_fmv[0], (long long)g->d_fmv[1]);
    /* K: context.night.code:still,slow,moving:d16,d32,d48,d64,d64+:dy_sum:len_sum (the still steps' three):ahead0..9
     * (the steps under 0.1 by the cell ahead) */
    fprintf(f, "K\t%d\t%d\t%s\t", i, g->nk, why);
    any = 0;
    for (int c = 0; c < 4; ++c)
        for (int n = 0; n < 2; ++n)
            for (int k = 0; k < 64; ++k)
            {
                const int64_t *q = g->k_n[c][n][k], *d = g->k_d[c][n][k];
                if (!(q[0] | q[1] | q[2])) continue;
                fprintf(f, "%s%d.%d.%d:%lld,%lld,%lld:%lld,%lld,%lld,%lld,%lld:%lld:%lld:", any++ ? " " : "", c, n, k,
                        (long long)q[0], (long long)q[1], (long long)q[2], (long long)d[0], (long long)d[1],
                        (long long)d[2], (long long)d[3], (long long)d[4], (long long)g->k_dy[c][n][k],
                        (long long)g->k_len[c][n][k]);
                for (int j = 0; j < 11; ++j) fprintf(f, "%s%lld", j ? "," : "", (long long)g->k_f[c][n][k][j]);
            }
    fputs(any ? "\n" : "-\n", f);
    /* KC: context.night.walk(0 sho_follow walking, 1 walking sneaking, 2 the spiral's leg):value:steps (under 0.1) */
    fprintf(f, "KC\t%d\t%d\t%s\t", i, g->nk, why);
    any = 0;
    for (int c = 0; c < 4; ++c)
        for (int n = 0; n < 2; ++n)
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 128; ++j)
                    if (g->k_c[c][n][k][j])
                        fprintf(f, "%s%d.%d.%d:%d:%lld", any++ ? " " : "", c, n, k, j, (long long)g->k_c[c][n][k][j]);
    fputs(any ? "\n" : "-\n", f);
}
static void dg_nbegin(struct dg_acc *g)
{
    g->dseen = 0, g->d_steps = g->d_an = g->d_any = 0;
    memset(g->d_bs, 0, sizeof g->d_bs), memset(g->d_own, 0, sizeof g->d_own), memset(g->d_near, 0, sizeof g->d_near);
    memset(g->d_pv, 0, sizeof g->d_pv), memset(g->d_sc, 0, sizeof g->d_sc), memset(g->d_fmv, 0, sizeof g->d_fmv);
    memset(g->k_n, 0, sizeof g->k_n), memset(g->k_d, 0, sizeof g->k_d), memset(g->k_dy, 0, sizeof g->k_dy);
    memset(g->k_len, 0, sizeof g->k_len), memset(g->k_f, 0, sizeof g->k_f), memset(g->k_c, 0, sizeof g->k_c);
    g->d_fb0 = g->d_fbl;
    g->d_f0 = g->d_fp0 = g->d_raw0 = g->d_ck0 = g->d_f1 = g->d_fp1 = g->d_raw1 = g->d_ck1 = -1;
    g->nsteps = g->nsky = g->nmove = 0, g->dist = 0.0;
    memset(g->nid, 0, sizeof g->nid);
    g->gr_n = g->gr_cn = g->gr_low = 0, g->gr_y = 0.0;
    memset(g->gr_c, 0, sizeof g->gr_c), memset(g->gr_bio, 0, sizeof g->gr_bio), memset(g->gr_nid, 0, sizeof g->gr_nid);
    g->gr_ntr = g->gr_trover = g->gr_pos = 0, g->gr_trunc = 0, g->gr_np = 0;
    memset(g->q_st, 0, sizeof g->q_st), memset(g->q_cl, 0, sizeof g->q_cl), memset(g->q_rc, 0, sizeof g->q_rc);
    memset(g->o_n, 0, sizeof g->o_n), memset(g->o_es, 0, sizeof g->o_es), memset(g->o_ec, 0, sizeof g->o_ec);
    memset(g->o_own, 0, sizeof g->o_own), memset(g->o_ph, 0, sizeof g->o_ph), memset(g->o_nid, 0, sizeof g->o_nid);
    g->a_n = g->a_capn = g->a_cap = g->a_load = g->a_loadw = g->a_lmax = 0, g->a_capmax = g->a_view = 0;
    memset(g->a_mon, 0, sizeof g->a_mon), memset(g->a_end, 0, sizeof g->a_end), memset(g->a_e64, 0, sizeof g->a_e64);
    memset(g->a_cs, 0, sizeof g->a_cs), memset(g->a_nid, 0, sizeof g->a_nid);
    memset(g->w_own, 0, sizeof g->w_own), memset(g->w_ph, 0, sizeof g->w_ph), memset(g->w_why, 0, sizeof g->w_why);
    memset(g->w_gate, 0, sizeof g->w_gate), memset(g->w_ob, 0, sizeof g->w_ob), memset(g->w_src, 0, sizeof g->w_src);
    memset(g->w_wood, 0, sizeof g->w_wood), memset(g->w_pvx, 0, sizeof g->w_pvx);
    memset(g->w_idle, 0, sizeof g->w_idle);
}

/* --sight-log (lane/pdig, orders 7:06 AM; read only; the census's alive test PRIVILEGED, a trace's only): a row an
 * enderman (entity id) seen in view (neth_rl_far, vis's rule), closed when it is gone from the living endermen within 128
 * (killed: the stats' endermen killed rose since it was last alive; else gone: despawned, or died otherwise), when it
 * has been out of view SG_LOST steps alive (lost), or at the attempt's end (end). Its first sighting's step, day time,
 * distance from the eye to its middle, the pearl phase then (PP_*, 12 the hunt off) and the owning macro bit; steps in
 * view, the last; the first step it screamed in view (-1 never), the least distance in view; a dawn crossed while
 * tracked; at the close its distance from the feet (-1 gone) and the feet under the sky */
#define SG_N 32
#define SG_LOST 1200
struct sg_ent {
    int32_t id, ph, ek_alive;
    uint32_t mb;
    int64_t first, wt0, last_view, scream, last_alive, view_steps;
    float d0, dmin, dend;
    int dawn, sky, miss;        /* miss: steps in a row absent from the living endermen within 128 */
    /* lane/pdig (F, read only): the fight from the scream: the pearl book's hits at the scream and now, the player's
     * health at the scream, its least since and the damage taken (the drops summed), the pearl phase and the feet
     * under the sky at the scream; the enderman's teleports (a census place 4 or more from the step before's), the
     * last one's step and the last hit's; after its last step in view: the player's blocks moved and its owner bit
     * and pearl phase at the end, the day clock then, the enderman's sky */
    int32_t h0, h1, ph_s, sky_s, tp, ph_e;
    int64_t tp_last, hit_last;
    float hp_s, hp_min, hurt, hp_prev, lv_x, lv_z, mv_after, ex, ey, ez;
    uint32_t mb_e;
    int has_e;
};
#define SG_MISS 20              /* ... this many: gone (a teleporting enderman is absent a step: 1354, g1 attempt 49) */
struct sg_acc {
    int n;
    struct sg_ent e[SG_N];
    int64_t day_last;
};
static void sg_row(FILE *f, int i, const struct sg_ent *s, const char *why)
{
    fprintf(f, "%d\t%d\t%lld\t%lld\t%.1f\t%d\t%u\t%lld\t%lld\t%lld\t%.1f\t%s\t%.1f\t%d\t%d", i, s->id,
            (long long)s->first, (long long)s->wt0, s->d0, s->ph, s->mb, (long long)s->view_steps,
            (long long)s->last_view, (long long)s->scream, s->dmin, why, s->dend, s->sky, s->dawn);
    fprintf(f, "\t%d\t%.1f\t%.1f\t%.1f\t%d\t%d\t%d\t%lld\t%lld\t%.1f\t%u\t%d\n", s->scream >= 0 ? s->h1 - s->h0 : -1,
            s->hp_s, s->hp_min, s->hurt, s->ph_s, s->sky_s, s->tp, (long long)s->tp_last, (long long)s->hit_last,
            s->mv_after, s->mb_e, s->ph_e);
}
static void dg_vrow(FILE *f, int i, const struct dg_visit *v, const char *why)
{
    fprintf(f, "V\t%d\t%d\t%s\t%lld", i, v->id, why, (long long)v->wt0);
    for (int b = 0; b < 3; ++b)
        fprintf(f, "\t%lld,%lld,%lld,%lld", (long long)v->f[b], (long long)v->l[b], (long long)v->st[b][0],
                (long long)v->st[b][1]);
    fprintf(f, "\t%.2f\t%.2f", v->dmin, v->dydmin);
    for (int b = 0; b < 2; ++b)
    {
        if (v->f[1 - b] < 0)
        {
            fputs("\t-\t-", f);
            continue;
        }
        fprintf(f, "\t%.1f,%.1f,%.1f,%d\t", v->e[b][0], v->e[b][1], v->e[b][2], v->sky[b]);
        for (int k = 0; k < NETH_RL_DIG_K; ++k) fprintf(f, "%s%d", k ? "," : "", v->c[b][k]);
    }
    fputc('\n', f);
}

/* --night-log (lane/pnight): an attempt's counts over one night (from the last dawn, or the start, to the next dawn).
 * What the screen shows: the endermen in view (neth_rl_far, vis's rule) by id, those screaming, the steps with one in view;
 * the stats screen's endermen killed; pearls and eyes held. PRIVILEGED (the census's, counting only): the endermen alive
 * within 64 and 128 blocks summed over the night's living steps (day time 13000 to 23000). The pearl macro's steps by
 * phase (PP_*, its own books) and the steps by owning macro bit (32 none) */
#define NL_IDS 64
struct nl_acc {
    int32_t seen_id[NL_IDS], scream_id[NL_IDS];
    int nseen, nscream;
    int64_t view_steps, night_steps, alive64, alive128, steps;
    int64_t phase[12], own[33];
    int32_t ek_at, held_at;     /* the stats' endermen killed and the pearls and eyes held when the night began */
    float hmin;                 /* the night's lowest health */
    /* the food day (what the inventory and the screen show): hunger points of food held gained and spent (each step's
     * rise and fall of food_points), the food animals (pig, cow, mooshroom, chicken) seen in view by id, the stats'
     * mobs killed (every kind) when the night began, the last step's food points held */
    int32_t food_id[NL_IDS];
    int nfood_seen;
    int64_t fp_gain, fp_spent;
    int32_t mk_at, fp_last;
    /* the pearl book's PG_WHY steps, its other macros' steps (whom) and neth_vec_pearl_gate's split of the policy
     * steps: at the night's start (0) and as last read (1) */
    int32_t why[2][12], whom[2][33], gate[2][3];
    int books_set;
    /* each enderman's first step in view this night: the pearl phase then (PP_*, 12 the hunt off) and its distance from
     * the eye (under 8, 8-16, 16-32, 32 and over); the pearl book's oracle walk (pearl_oracle) at the night's start (0)
     * and as last read (1): its steps, those by the target's distance (under 16, 16-32, 32-64, 64 and over), on the
     * pathfinder's way, targets marked with no path, pillars for one near; the travel leg's steps (pearl_home) */
    int32_t fs_ph[13], fs_d[4];
    int32_t ob[2][9];
    /* lane/pnight (2:58 AM order; PRIVILEGED, the census's, a trace's only): the night's enderman-steps within 64
     * (horizontally) by where each stood against the player: its middle under the eye by over 6, within 6, over it by
     * over 6; its feet under the open sky; the line clear from the eye, from 4 and 8 blocks over it, from the high
     * ground's eye (hi); and the first sightings' places: the player's feet under the open sky, 2 or more blocks over
     * the ground under them (a pillar or a brink), under y 62; nr[8] (3:44 AM order): enderman-steps within 128 with
     * the feet under the open sky (the surface's supply) */
    int64_t nr[9];
    int32_t fs_st[3];
    /* lane/pnight (5:19 AM order; PRIVILEGED, neth_rl_tower, a trace's only): the night's steps of endermen under the
     * sky within 64 and 128, those with the line clear from the eye raised 0, 5, 10 and 20 blocks, and of those the
     * ones within the pixel test's reach (NETH_RL_TOWER_R) of that eye: the watchtower's ceiling */
    int64_t tw_n[2], tw_c[2][NETH_RL_TOWER_NH], tw_r[2][NETH_RL_TOWER_NH];
};
static int nl_add_id(int32_t *ids, int *n, int32_t id)
{
    for (int k = 0; k < *n; ++k)
        if (ids[k] == id) return 0;
    if (*n < NL_IDS) ids[(*n)++] = id;
    return 1;
}
static void nl_ob(int32_t ob[9], const struct neth_vec_pearl *pn)
{
    ob[0] = pn->oracle_steps;
    for (int k = 0; k < 4; ++k) ob[1 + k] = pn->oracle_d[k];
    ob[5] = pn->oracle_path, ob[6] = pn->oracle_bad, ob[7] = pn->oracle_ups, ob[8] = pn->home_steps;
}
/* one --night-log row: attempt i's night e->nl_k ended (dawn) or its attempt did (result); ek the stats' endermen
 * killed now (-1 not read), held the pearls and eyes held now */
static void nl_row(FILE *f, int i, const struct env *e, const char *result, const struct view *w, int ek, int held,
                   int mk)
{
    const struct nl_acc *a = e->nl;
    fprintf(f, "%d\t%d\t%s\t%lld\t%lld\t%.1f\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t", i, e->nl_k,
            result, (long long)e->n, (long long)(w->p.world_time % 24000), w->p.health, w->p.food,
            food_points(&w->p.st), w->armor, held, held - a->held_at, a->hmin,
            ek >= 0 && a->ek_at >= 0 ? ek - a->ek_at : -1, a->nseen, a->nscream, (long long)a->view_steps,
            (long long)a->night_steps, (long long)a->alive64, (long long)a->alive128);
    for (int k = 0; k < 12; ++k) fprintf(f, "%s%lld", k ? "," : "", (long long)a->phase[k]);
    fputc('\t', f);
    int any = 0;
    for (int k = 0; k < 33; ++k)
        if (a->own[k]) fprintf(f, "%s%d:%lld", any++ ? "," : "", k, (long long)a->own[k]);
    fprintf(f, "%s\t%lld\t%lld\t%d\t%d\t", any ? "" : "-", (long long)a->fp_gain, (long long)a->fp_spent, a->nfood_seen,
            mk >= 0 && a->mk_at >= 0 ? mk - a->mk_at : -1);
    for (int k = 0; k < 12; ++k) fprintf(f, "%s%d", k ? "," : "", a->why[1][k] - a->why[0][k]);
    fputc('\t', f);
    any = 0;
    for (int k = 0; k < 33; ++k)
        if (a->whom[1][k] != a->whom[0][k]) fprintf(f, "%s%d:%d", any++ ? "," : "", k, a->whom[1][k] - a->whom[0][k]);
    fprintf(f, "%s\t", any ? "" : "-");
    for (int k = 0; k < 3; ++k) fprintf(f, "%s%d", k ? "," : "", a->gate[1][k] - a->gate[0][k]);
    fputc('\t', f);
    for (int k = 0; k < 13; ++k) fprintf(f, "%s%d", k ? "," : "", a->fs_ph[k]);
    fputc('\t', f);
    for (int k = 0; k < 4; ++k) fprintf(f, "%s%d", k ? "," : "", a->fs_d[k]);
    fputc('\t', f);
    for (int k = 0; k < 9; ++k) fprintf(f, "%s%d", k ? "," : "", a->ob[1][k] - a->ob[0][k]);
    fputc('\t', f);
    for (int k = 0; k < 9; ++k) fprintf(f, "%s%lld", k ? "," : "", (long long)a->nr[k]);
    fputc('\t', f);
    for (int k = 0; k < 3; ++k) fprintf(f, "%s%d", k ? "," : "", a->fs_st[k]);
    fputc('\t', f);
    for (int b = 0; b < 2; ++b)
    {
        fprintf(f, "%s%lld", b ? ";" : "", (long long)a->tw_n[b]);
        for (int j = 0; j < NETH_RL_TOWER_NH; ++j) fprintf(f, ",%lld", (long long)a->tw_c[b][j]);
        for (int j = 0; j < NETH_RL_TOWER_NH; ++j) fprintf(f, ",%lld", (long long)a->tw_r[b][j]);
    }
    fputc('\n', f);
    fflush(f);
}
/* a new night's counts from now: ek the stats' endermen killed, held the pearls and eyes held, h the health, mk the
 * stats' mobs killed, fp the food points held */
static void nl_begin(struct nl_acc *a, int ek, int held, float h, int mk, int fp)
{
    int32_t why[12], whom[33], gate[3], ob[9];
    int set = a->books_set;
    memcpy(why, a->why[1], sizeof why), memcpy(whom, a->whom[1], sizeof whom), memcpy(gate, a->gate[1], sizeof gate);
    memcpy(ob, a->ob[1], sizeof ob);
    memset(a, 0, sizeof *a);
    a->ek_at = ek, a->held_at = held, a->hmin = h, a->mk_at = mk, a->fp_last = fp;
    if (set)    /* the next night's books start where this one's end */
    {
        memcpy(a->why[0], why, sizeof why), memcpy(a->whom[0], whom, sizeof whom), memcpy(a->gate[0], gate, sizeof gate);
        memcpy(a->why[1], why, sizeof why), memcpy(a->whom[1], whom, sizeof whom), memcpy(a->gate[1], gate, sizeof gate);
        memcpy(a->ob[0], ob, sizeof ob), memcpy(a->ob[1], ob, sizeof ob);
        a->books_set = 1;
    }
}

/* --death-trace (lane/proom): a step of the ring. PRIVILEGED fields (the census's: nearest hostile by kind, the ground
 * under the feet, the open sky) are a trace's only; nothing here is read by a macro */
#define DT_RING 400
struct dt_row {
    int64_t step, wt;
    uint32_t mbit;
    int8_t kind, pon, phase, sky, attack, censon, fire, onground, prov;
    float x, y, z, ground, health, hnear[5];
    int16_t food, food_pts, armor, held, tid_angry, nfar, hostile[4], skel_los;
    float tdist;
    /* lane/pnight (what the screen shows): the nearest skeleton in view (neth_rl_state vis: within 16 blocks, the cone,
     * the line clear) from the eye, -1 none; the crosshair's living's kind (mo_ekind, -1 none); the look's yaw and
     * pitch */
    float vskel, yaw, pitch;
    int16_t mo_kind;
    int16_t vkind;              /* the nearest living in view that is not a food animal (vis), its kind (-1 none) */
    float vdist;                /* ... its distance from the eye */
    /* PRIVILEGED (neth_rl_combat): the combat tracker's last entry's damage type (chatmsg.h DT_*), its source's name, its
     * damage, and the longest fall's length; the ticks since that entry landed */
    int16_t cm_dt, cm_name;
    float cm_damage, cm_fall;
    int32_t cm_age;
    uint32_t hold;              /* lane/pnight: the stepped act's held keys (PK_* bits; edge_guard's sneak) */
    /* lane/pground (read only): the pearl macro's target (0 none), the crosshair's entity at its last drive, the
     * target in its view list, its clock since the last swing; the target's bearing off the look's yaw and its pitch
     * off the look's (degrees, from the eye to the box middle; 0 without a target); the stats' endermen killed */
    int32_t tid, mo_ent, tvis, thit_age, ekills;
    float tbear, tpit;
};
/* --lava-log (lane/lava, books only, read only): a step of the ring. The macros' own senses (neth_vec_lava: lava in
 * view, the lava options' and the strip's tests) and the act; PRIVILEGED (a trace's only, nothing here is read by a
 * macro): the client world's cells round the body (lv_blocks: what the player was sent, walls not in the way) */
#define LV_RING 400
#define LV_BOX 4                /* the PRIVILEGED box: LV_BOX blocks round the feet across, LV_BOX under, LV_BOX + 1 over */
struct lv_row {
    int64_t step, lava_t;
    uint32_t mbit, hold;
    int8_t kind, lsrc, fire, og, eye_water, edge, st_hot, st_near, ob_hot, esc, sdir, press_att, press_use, hotbar;
    float x, y, z, yaw, pitch, health, hot_d;
    int16_t hot_n, mo_hit, mo_dx, mo_dy, mo_dz, mo_block, lq_hit, lq_block, held;
    float lq_d;
    /* PRIVILEGED: the feet's, the head's and the floor's blocks; lava cells (10, 11) touching the body's two cells (the
     * six faces of each and the floor's), within 1 and within LV_BOX of the feet (Chebyshev), the nearest's offset; the
     * crosshair block's face neighbours that are lava */
    int16_t b_feet, b_head, b_under, lv_touch, lv_1, lv_box, lv_dx, lv_dy, lv_dz, mo_lavanb;
    int16_t fire_t;             /* the fire's ticks left (the player's own) */
    float water_d;              /* water (a source at the surface) in view within LV_VIEW_R: its distance, -1 none */
};
#define DT_SKELETON 11          /* engine/living.h HK_SKELETON: neth_rl_state vis's kind */
#define DT_FOOD_LAST 3          /* engine/living.h AK_CHICKEN: vis's food animals are kinds 0 to 3 */
#define DT_HEADER                                                                                                      \
    "attempt\tbefore\tstep\twt\tmbit\tkind\tpearl_on\tphase\tx\ty\tz\tground\tsky\thealth\tfood\t"                    \
    "food_pts\tarmor\theld\tattack\tfire\tonground\tangry\ttdist\tnfar\tcensus\tn_skel\tn_zomb\t"                      \
    "n_spider\tn_creeper\tskel_los\td_skel\td_zomb\td_spider\td_creeper\td_ender\tprov\t"                              \
    "vskel\tyaw\tpitch\tmo_kind\tvkind\tvdist\tcm_dt\tcm_name\tcm_damage\tcm_fall\tcm_age\thold\t"                     \
    "tid\tmo_ent\ttvis\tthit_age\ttbear\ttpit\tekills\n"

/* --death-trace's and --kill-trace's rows: attempt i's ring, its last DT_RING steps oldest first (before: steps from
 * the ring's newest) */
static void dt_dump(FILE *f, int i, const struct dt_row *ring, int64_t dt_n)
{
    int64_t n = dt_n < DT_RING ? dt_n : DT_RING;
    for (int64_t k = dt_n - n; k < dt_n; ++k)
    {
        const struct dt_row *q = &ring[k % DT_RING];
        fprintf(f,
                "%d\t%lld\t%lld\t%lld\t%u\t%d\t%d\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%.1f\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%d\t"
                "%.1f\t%.1f\t%.1f\t%d\t%d\t%.1f\t%d\t%d\t%.1f\t%.1f\t%d\t%x\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%d\n",
                i, (long long)(dt_n - 1 - k), (long long)q->step, (long long)q->wt, q->mbit, q->kind, q->pon,
                q->phase, q->x, q->y, q->z, q->ground, q->sky, q->health, q->food, q->food_pts, q->armor, q->held,
                q->attack, q->fire, q->onground, q->tid_angry, q->tdist, q->nfar, q->censon, q->hostile[0],
                q->hostile[1], q->hostile[2], q->hostile[3], q->skel_los, q->hnear[0], q->hnear[1], q->hnear[2],
                q->hnear[3], q->hnear[4], q->prov, q->vskel, q->yaw, q->pitch, q->mo_kind, q->vkind, q->vdist,
                q->cm_dt, q->cm_name, q->cm_damage, q->cm_fall, q->cm_age, q->hold, q->tid, q->mo_ent, q->tvis,
                q->thit_age, q->tbear, q->tpit, q->ekills);
    }
    fflush(f);
}

static int64_t book_delta(int64_t now, int64_t before)
{
    return now >= before ? now - before : now; /* a counter reset on a new episode starts from zero */
}

/* --resume DIR: the K nodes of an earlier search to play again first (the top rungs, at most K/4 + 1 a rung, the
 * best kept first) */
static int resume_pick(const char *dir, int k, int *ids, int *rungs, int rmax, int rmin, const char *bad, const char *only)
{
    char p[2200];
    snprintf(p, sizeof p, "%s/nodes.tsv", dir);
    FILE *f = fopen(p, "r");
    if (!f) return fprintf(stderr, "chain: --resume %s: %s\n", p, strerror(errno)), -1;
    int cap = 1 << 16, nn = 0;
    int *id = malloc((size_t)cap * sizeof *id), *rg = malloc((size_t)cap * sizeof *rg);
    float *q = malloc((size_t)cap * sizeof *q);
    int *par = malloc((size_t)cap * sizeof *par);
    char line[8192];
    while (fgets(line, sizeof line, f) && nn < cap)
    {
        int a, b, c;
        float d;
        char name[64];
        if (line[0] == '#' || sscanf(line, "%d\t%d\t%d\t%63s\t%f", &a, &b, &c, name, &d) != 5) continue;
        int byname = rung_by_name(name);       /* (the name: a ladder with a rung added since renumbers) */
        int r = byname >= 0 ? byname : c;
        /* (lane/chain2: a row with the prog column ranked by the pick's kept score, q and the progress weighted) */
        int t = 0;
        for (const char *s = line; *s; ++s) t += *s == '\t';
        const char *last = strrchr(line, '\t');
        if (t >= 15 && last) d += (r >= R_END ? CH_PROG_W_END : CH_PROG_W) * (float)atoi(last + 1);
        id[nn] = a, rg[nn] = r, q[nn] = d, par[nn] = b, ++nn;
    }
    fclose(f);
    if (only && only[0])
    {
        /* --resume-ids A,B,...: those nodes, in that order */
        int got = 0;
        for (const char *s = only; *s && got < k;)
        {
            int want = atoi(s);
            for (int j = 0; j < nn; ++j)
                if (id[j] == want)
                {
                    if (rungs) rungs[got] = rg[j];
                    ids[got++] = want;
                    break;
                }
            while (*s && *s != ',') ++s;
            if (*s == ',') ++s;
        }
        free(id), free(rg), free(q), free(par);
        return got;
    }
    /* --bad's list (the earlier search's verifier): those nodes and their descendants not resumed */
    FILE *bf = bad && bad[0] ? fopen(bad, "r") : NULL;
    if (bf)
    {
        int mx = 0;
        for (int j = 0; j < nn; ++j) mx = id[j] > mx ? id[j] : mx;
        char *isbad = calloc((size_t)mx + 1, 1);
        int b, nb = 0;
        while (fscanf(bf, "%d%*[^\n]", &b) == 1)
            if (b >= 0 && b <= mx) isbad[b] = 1;
        fclose(bf);
        for (int j = 0; j < nn; ++j)
        {
            if (par[j] >= 0 && par[j] <= mx && isbad[par[j]]) isbad[id[j]] = 1;
            if (isbad[id[j]]) rg[j] = -1, ++nb;
        }
        printf("resume: %d of %s's nodes under --bad's list left out\n", nb, dir);
        free(isbad);
    }
    free(par);
    int got = 0, per = rmin > 0 ? k : k / 4 + 1;      /* (--min-rung: every resumed node may be of one rung) */
    for (int r = rmax; r >= rmin && got < k; --r)
    {
        int took = 0;
        while (took < per && got < k)
        {
            int best = -1;
            for (int j = 0; j < nn; ++j)
                if (rg[j] == r && (best < 0 || q[j] > q[best])) best = j;
            if (best < 0) break;
            if (rungs) rungs[got] = r;
            ids[got++] = id[best];
            rg[best] = -1;
            ++took;
        }
    }
    free(id), free(rg), free(q);
    return got;
}

struct search {
    struct neth_vec *v;
    struct nwrl *h;
    struct node *nd;
    int nn, ncap;
    int keep, live;
    int cell;                   /* --cell C: the frontier's place key, C-block cells of (x, z, dimension): a rung's
                                 * segments drawn across its cells, its images let go from its fullest cell first
                                 * (0: off) */
    int64_t seg_len;
    int64_t water_seg;            /* --water-seg S: a three-diamond water-bucket node plays S game seconds */
    int64_t land_seg;             /* --land-seg S: an on_land node plays S game seconds before a station exists */
    int64_t day_segs;           /* --dayff: the day segments played */
    int64_t day_died, day_saved;   /* ... dead, and the nodes saved from them */
    int64_t late_seg;           /* --late-seg S: segments from eyes_12 on, S seconds (0: --seg's) */
    int64_t snow_seg;           /* --snow-seg S: shorter travel saves at snowballs_64 only, S seconds (0: --late-seg) */
    int snow_min;               /* --snow-min 16|32|64: the counted snowball kit; the macro's snow_target must match */
    int spent;                  /* --spent N: a node with N segments played and no node saved is drawn no more (0: off) */
    int dayff, day_live;      /* --dayff K: at most K segments at once from a pearl node in the day (home to the
                                 * twelfth pearl, the clock CH_DAY0..CH_DUSK), each played to dusk; the others wait
                                 * (0: off); the day segments playing now */
    int free_max, save_wait;
    int prov;                   /* --provisions: hunger points of food a chain holds with the portal's kit before any
                                 * rung past it counts (0: off) */
    int meat;                   /* --meat: meats held after the furnace before any rung past the meat rung counts */
    int obs_health_min, obs_food_min; /* opt-in survival floor for obsidian and provisions rungs */
    int toward;                 /* --toward X,Z: the on_land rung and the way there's progress toward (X, Z) */
    int room_score_macro;       /* --room-score-macro: snow-to-room distance uses the controller's located room */
    int room_score_prospective; /* --room-score-prospective: before that latch, use its room selector at these feet */
    double tx, tz;
    int chamber_stronghold;     /* --mopt nether_chamber=stronghold saves each tunnel leg */
    int kit;                    /* --kit: the End's kit (iron armor worn, 64 snowballs) before the stronghold counts */
    int armor_min;              /* --armor-min N: armor points required at the armor rung and before End gains */
    int endfood;                /* --endfood P (lane/chain2): food points held before the stronghold room counts (0 off) */
    int post_stock_food;        /* --post-stock-food P: after food_stock, End floor P and continuous travel score */
    int provision_on;           /* food_provision=on or armor_first: food_stock before snowballs */
    int late_health_min, late_food_min; /* --late-health-min, --late-food-min: floor to count snow or room (0 off) */
    int end_fed;                /* --end-fed 1: and an End gain only on a chain that held them at its last step outside */
    int end_armor;              /* --end-armor 1: an End gain only on a chain in iron armor at its last step outside */
    int min_rung;               /* --min-rung R (lane/chain2): segments drawn, and nodes resumed, from rung R and over (0: any) */
    int64_t save_busy;          /* lane/chain2: saves refused (every save slot busy) */
    int64_t floor_cut[2];       /* lane/chain2: gains not saved under CH_END_HP health: in the End, elsewhere */
    int64_t end_seg;            /* --end-seg S: End segments of S steps, each ended at its first save (0: --late-seg's) */
    struct node gone[CH_GONE];  /* lane/chain2: the last nodes let go, as they were, and their eviction scores */
    float gone_sc[CH_GONE];
    int64_t ngone;
    int hold;                   /* --resume: groups with no env replaying held back until the replays are done (lane/chain2) */
    int carry;                  /* --carry: obsidian held with the flint and steel before any rung past obsidian_20
                                 * counts (0: off; run with obsidian_carry and nether_portal_home) */
    char bad_path[2048];        /* --bad FILE (bad_load) */
    char dir[2048];
    FILE *ftsv;
    double t0;
    int64_t first_t[R_N];       /* the node that first reached each rung, -1 */
    double first_wall[R_N];
    int64_t saves, saves_failed, freed, deaths, segs;
    int64_t drive[R_N][33];     /* the steps by the segment's start rung and who drove them (0 the policy, 1 + the
                                 * label's switch bit) */
    int64_t hm[14];             /* the way home's segments (rod_7 to home): played, died; deaths by cause (lava, fire,
                                 * fall, mob, starve, other); by distance from the spawner (under 8, 8-20, 20-60, 60 and
                                 * over, none known); within 10 of the arrival portal */
    int64_t fb[12];             /* the food census summed over the segments' ends: scouts, found, ended out of time,
                                 * below the surface, water, night or held off, cut short; hunts, meat got, ended with
                                 * meat, lost, out of time */
    int64_t eb[10];             /* the End's books summed over segments from the end rung (chain.c's End line) */
    int64_t ed[40];            /* lane/chain2: the End's segments in detail (vecenv.h neth_vec_dragon_info [21] .. [76]): [0..8] the
                                 * deaths by DK_*, [9] pushed before, [10..13] the death's step (under 3,000, to 10,000, to
                                 * 25,000, over), [14] fall lengths summed, [15] the food at the deaths summed, [16..18] the
                                 * dragon's lowest health at 150, 100, 50 or under, [19] segments it was seen, [20] health the
                                 * swings took, [21] its health the arrows took, [22] the boss bar's rises, [23..31] the
                                 * macro's phase at the death (0 .. 8) */
    int64_t pl[66];            /* the pearls' books summed over the segments' ends (home to the twelfth pearl):
                                 * segments, endermen seen, provoked, killed, pillars, deaths, the 12 phases' steps,
                                 * pearl_oracle's walking steps, steps with a pearl item within 32 blocks, the deaths'
                                 * causes (10), ..., [61..65] the provoked locks' ends (prov_end) */
    int64_t bp[6];              /* the bucket portal's books summed over the segments' ends: starts, sites, casts, lit,
                                 * lava trips, search steps */
    int64_t ddeath[R_N];        /* deaths by the segment's start rung */
};

static int mkdirs(const char *d)
{
    char b[4096];
    snprintf(b, sizeof b, "%s", d);
    for (char *p = b + 1; *p; ++p)
        if (*p == '/')
        {
            *p = 0;
            mkdir(b, 0755);
            *p = '/';
        }
    return mkdir(b, 0755) && errno != EEXIST ? -1 : 0;
}

static struct node *node_new(struct search *S)
{
    if (S->nn == S->ncap)
    {
        S->ncap = S->ncap ? 2 * S->ncap : 1024;
        S->nd = realloc(S->nd, (size_t)S->ncap * sizeof *S->nd);
    }
    struct node *n = &S->nd[S->nn];
    memset(n, 0, sizeof *n);
    n->id = S->nn++;
    return n;
}

static int live_count(const struct search *S, int rung)
{
    int c = 0;
    for (int k = 0; k < S->nn; ++k)
        if ((S->nd[k].state == NS_READY || S->nd[k].state == NS_MAKING) && S->nd[k].start >= 0 &&
            (rung < 0 || S->nd[k].rung == rung))
            ++c;
    return c;
}

/* --cell: a node's place, (x / C, z / C, dimension) as one key */
static int64_t cell_of(const struct search *S, const struct node *n)
{
    int64_t cx = (int64_t)floor(n->p.x / S->cell), cz = (int64_t)floor(n->p.z / S->cell);
    return ((cx & 0xFFFFF) << 24) | ((cz & 0xFFFFF) << 4) | (int64_t)(n->p.dim + 1);
}

/* --cell: a cell whose rung has had CH_CELL_AGE segments and no gain is aged out: its share CH_CELL_OLD of a cell's
 * (e42p, easy: 299 water_bucket states at one lava site, 857 segments, no diamond) */
#define CH_CELL_AGE 64
#define CH_CELL_OLD 0.1
#define CH_CELL_MAX 512

/* a node's progress weight in its score (lane/chain2: in the End the progress first, so a save's node outranks its
 * parent; s42k2-22 drew one dragon_75 node for 14 minutes over 1,352 shallow children of equal q) */
static float prog_w(const struct node *n) { return n->rung >= R_END ? CH_PROG_W_END : CH_PROG_W; }

/* a node's two scores: the pick's (the best drawn; without its noise) and the eviction's (the worst let go) */
static float score_pick(const struct node *n)
{
    return n->q + prog_w(n) * (float)n->prog - CH_TRY_W * log2f(1.0f + (float)n->tries + (float)n->held);
}
static float score_evict(const struct node *n)
{
    /* (the gains' bonus capped at CH_GAIN_CAP, lane/chain2: uncapped, s42k2-23's resumed End parent with 76 saved
     * children scored +38 and every child, one End progress over it, was let go before its first try) */
    return n->q + prog_w(n) * (float)n->prog - CH_TRY_W * log2f(1.0f + (float)n->tries) +
           0.5f * (float)(n->gain_tries < CH_GAIN_CAP ? n->gain_tries : CH_GAIN_CAP);
}

/* let images go: over K a rung, then over L in all, the worst-kept of the fullest rung first (--cell: of its fullest
 * cell); never node 0, one an env plays from, or one a save replays from */
static void evict(struct search *S)
{
    for (int round = 0; round < 64; ++round)
    {
        int cnt[R_N] = {0}, tot = 0;
        for (int k = 1; k < S->nn; ++k)
            if (S->nd[k].state == NS_READY && S->nd[k].start >= 0) ++cnt[S->nd[k].rung], ++tot;
        int rung = -1;
        for (int r = 0; r < R_N; ++r)
            if (cnt[r] > S->keep && (rung < 0 || cnt[r] > cnt[rung])) rung = r;
        if (rung < 0 && live_count(S, -1) > S->live)
            for (int r = 0; r < R_N; ++r)
                if (cnt[r] > 1 && (rung < 0 || cnt[r] > cnt[rung])) rung = r;
        if (rung < 0) return;
        /* (--min-rung, lane/chain2: a node under it is never drawn, so its image goes first; s42k2-26 kept 13 of its 16
         * images on the replays' lower rungs, one a rung, and let every new dragon_75 node go at once, the only ones at
         * the rung no env held) */
        /* (and without it, a node CH_BEHIND rungs under the top rung with an image: s42k3 let rung-26 nodes go with
         * evict scores of 37 to 41 while rung-1 nodes held images) */
        int floor_r = S->min_rung, top = 0;
        if (floor_r <= 0)
        {
            for (int k = 1; k < S->nn; ++k)
                if (S->nd[k].state == NS_READY && S->nd[k].start >= 0 && S->nd[k].rung > top) top = S->nd[k].rung;
            floor_r = top - CH_BEHIND;
        }
        int low = -1;
        for (int k = 1; k < S->nn && floor_r > 0; ++k)
        {
            const struct node *n = &S->nd[k];
            if (n->state == NS_READY && n->start >= 0 && n->rung < floor_r && !n->held && !n->pend) { low = k; break; }
        }
        if (low >= 0)
        {
            neth_rl_start_free(S->h, S->nd[low].start);
            S->nd[low].start = -1;
            S->nd[low].state = NS_FREED;
            ++S->freed;
            continue;
        }
        /* --cell: the victim from the rung's fullest cell (of the nodes that may go) */
        int64_t vcell = 0;
        int vcn = 0;
        if (S->cell > 0)
        {
            static int64_t ck[CH_CELL_MAX];
            static int cn[CH_CELL_MAX];
            int nc = 0;
            for (int k = 1; k < S->nn; ++k)
            {
                struct node *n = &S->nd[k];
                if (n->state != NS_READY || n->start < 0 || n->rung != rung || n->held || n->pend) continue;
                int64_t c = cell_of(S, n);
                int j = 0;
                while (j < nc && ck[j] != c) ++j;
                if (j == nc && nc < CH_CELL_MAX) ck[nc] = c, cn[nc++] = 0;
                if (j < nc) ++cn[j];
            }
            for (int j = 0; j < nc; ++j)
                if (cn[j] > vcn) vcn = cn[j], vcell = ck[j];
        }
        int best = -1;
        float bs = 1e30f;
        /* (lane/chain2: a node in the End tried fewer than CH_EVICT_TRIES times is let go only when no other may) */
        for (int pass = 0; pass < 2 && best < 0; ++pass)
            for (int k = 1; k < S->nn; ++k)
            {
                struct node *n = &S->nd[k];
                if (n->state != NS_READY || n->start < 0 || n->rung != rung || n->held || n->pend) continue;
                if (vcn > 1 && cell_of(S, n) != vcell) continue;
                if (pass == 0 && n->rung >= R_END && n->tries < CH_EVICT_TRIES) continue;
                float sc = score_evict(n);
                if (sc < bs) bs = sc, best = k;
            }
        if (best < 0) return;
        S->gone[S->ngone++ % CH_GONE] = S->nd[best], S->gone_sc[(S->ngone - 1) % CH_GONE] = bs;
        neth_rl_start_free(S->h, S->nd[best].start);
        S->nd[best].start = -1;
        S->nd[best].state = NS_FREED;
        ++S->freed;
    }
}

/* the next segment's node: a rung by the weights over the rungs present, then its best-scored node or one at random */
/* --bad FILE: node ids whose chains a verifier found unequal to the Java client's rows (chainverify.sh), one a line;
 * each and every descendant marked bad (a parent's id is lower than its children's), never picked again */
static int bad_load(struct search *S)
{
    if (!S->bad_path[0]) return 0;
    FILE *f = fopen(S->bad_path, "r");
    if (!f) return 0;
    int id, n = 0;
    while (fscanf(f, "%d%*[^\n]", &id) == 1)
        if (id >= 0 && id < S->nn && !S->nd[id].bad) S->nd[id].bad = 1, ++n;
    fclose(f);
    for (int k = 0; k < S->nn; ++k)
        if (!S->nd[k].bad && S->nd[k].parent >= 0 && S->nd[S->nd[k].parent].bad) S->nd[k].bad = 1, ++n;
    if (n) printf("  --bad: %d nodes marked (listed, or under one)\n", n);
    return n;
}

/* --dayff: a node's steps to dusk's end when it is a pearl node in the day (iron armor to the eleventh pearl; the
 * hunt wants the night), else 0 */
static int64_t day_steps(const struct search *S, const struct node *n)
{
    if (S->dayff <= 0 || n->rung < R_ARMOR || n->rung >= R_PEARL12) return 0;
    int64_t t = ((n->p.world_time % 24000) + 24000) % 24000;
    return t >= CH_DAY0 && t < CH_DUSK ? CH_DUSK_END - t : 0;
}

/* a node not drawn now: --dayff, a day node while K day segments play; --spent N, N segments played from it and no
 * node saved from any (e43q's node 1159, pearl_11 at health 5.4: 270 segments, each dead at 194 steps on average) */
static int held_out(const struct search *S, const struct node *n)
{
    /* (not in the End: a fight that loses most tries is still the only way on) */
    if (S->spent > 0 && n->rung < R_END && n->tries >= S->spent && n->kids == 0) return 1;
    return S->dayff > 0 && S->day_live >= S->dayff && day_steps(S, n) > 0;
}

static int pick(struct search *S, uint64_t *rng)
{
    int present[R_N] = {0};
    for (int k = 0; k < S->nn; ++k)
        if (S->nd[k].state == NS_READY && S->nd[k].start >= 0 && !S->nd[k].bad && !held_out(S, &S->nd[k]))
            present[S->nd[k].rung] = 1;
    /* --min-rung R (lane/chain2): the segments from rung R and over while any such node keeps its image (the End's fight: the
     * frontier's weights gave the End about a third of the segments over 13 rungs present, s42k2-15) */
    if (S->min_rung > 0)
    {
        int any = 0;
        for (int r = S->min_rung; r < R_N; ++r) any |= present[r];
        for (int r = 0; any && r < S->min_rung; ++r) present[r] = 0;
    }
    double w[R_N] = {0}, tot = 0;
    int rank = 0;
    for (int r = R_N - 1; r >= 0; --r)
        if (present[r])
        {
            w[r] = rank == 0 ? CH_TOP_W : rank == 1 ? CH_NEXT_W : rank == 2 ? CH_THIRD_W : 1.0;
            tot += w[r];
            ++rank;
        }
    double u = xu(rng) * tot;
    int rung = 0;
    for (int r = 0; r < R_N; ++r)
        if (w[r] > 0)
        {
            rung = r;
            if (u < w[r]) break;
            u -= w[r];
        }
    /* --cell: a cell of the rung drawn first (each 1 share, an aged-out one CH_CELL_OLD: its rung's segments from
     * every node it ever held, kept or let go, and none saved a node over the rung), then a node of it */
    int64_t pcell = 0;
    int cellpick = 0;
    if (S->cell > 0)
    {
        static int64_t ck[CH_CELL_MAX];
        static int ct[CH_CELL_MAX], cg[CH_CELL_MAX], cl[CH_CELL_MAX];
        int nc = 0;
        for (int k = 0; k < S->nn; ++k)
        {
            struct node *n = &S->nd[k];
            if (n->rung != rung || n->bad || (n->state != NS_READY && n->state != NS_FREED)) continue;
            int64_t c = cell_of(S, n);
            int j = 0;
            while (j < nc && ck[j] != c) ++j;
            if (j == nc)
            {
                if (nc == CH_CELL_MAX) continue;
                ck[nc] = c, ct[nc] = cg[nc] = cl[nc] = 0, ++nc;
            }
            ct[j] += n->tries, cg[j] += n->gain_tries;
            if (n->state == NS_READY && n->start >= 0 && !held_out(S, n)) ++cl[j];
        }
        double cw[CH_CELL_MAX], ctot = 0;
        for (int j = 0; j < nc; ++j)
        {
            cw[j] = !cl[j] ? 0.0 : ct[j] >= CH_CELL_AGE && !cg[j] ? CH_CELL_OLD : 1.0;
            ctot += cw[j];
        }
        if (ctot > 0)
        {
            double cu = xu(rng) * ctot;
            for (int j = 0; j < nc; ++j)
                if (cw[j] > 0)
                {
                    pcell = ck[j], cellpick = 1;
                    if (cu < cw[j]) break;
                    cu -= cw[j];
                }
        }
    }
    int m = 0, best = -1;
    float bs = -1e30f;
    for (int k = 0; k < S->nn; ++k)
    {
        struct node *n = &S->nd[k];
        if (n->state != NS_READY || n->start < 0 || n->rung != rung || n->bad || held_out(S, n)) continue;
        if (cellpick && cell_of(S, n) != pcell) continue;
        ++m;
        float sc = score_pick(n) + 0.01f * (float)xu(rng);
        if (sc > bs) bs = sc, best = k;
    }
    if (xu(rng) < CH_BEST_P || m <= 1) return best;
    int j = (int)(xu(rng) * m);
    for (int k = 0; k < S->nn; ++k)
    {
        struct node *n = &S->nd[k];
        if (n->state != NS_READY || n->start < 0 || n->rung != rung || n->bad || held_out(S, n)) continue;
        if (cellpick && cell_of(S, n) != pcell) continue;
        if (j-- == 0) return k;
    }
    return best;
}

static void read_view(struct search *S, int i, struct view *w)
{
    memset(w, 0, sizeof *w);
    w->loc_d = -1;
    w->entry_d = -1;
    w->water_kit_on = S->water_seg > 0;
    neth_vec_chain_peek(S->v, i, &w->p);
    w->dragon_hp = -1;
    w->goal_d = -1;
    /* the armor points worn (lane/rod's armor_kit books; 0 without it) */
    struct neth_vec_kit kb;
    if (neth_vec_kit(S->v, i, &kb) == 0) w->armor = kb.points;
    /* (lane/chain2: the points the client's armor bar shows, the books' armor_worn: the kit's books read 0 after a
     * resume's replay, and the resumed chains were taken as armored; every s42k3 End state and s42k2's kill node held
     * an iron chestplate in the pack) */
    const struct neth_rl_far *fr = neth_rl_far(S->h, i);
    if (fr) w->armor = fr->armor_worn;
    if (w->p.dim == -1)
    {
        struct neth_vec_nether nb;
        memset(&nb, 0, sizeof nb);   /* a saved older variant may write only the fields before entry[] */
        if (neth_vec_nether(S->v, i, &nb) == 0)
        {
            w->fort_seen = nb.fort[0] != 0, w->on_fort = nb.fortin >= 0;
            if (nb.loc[0])
            {
                w->loc_d = hypot(nb.pos[0] - nb.loc[1], nb.pos[2] - nb.loc[2]);
                w->loc_d0 = nb.loc_d0;
            }
            if (nb.entry[0])
            {
                double dx = nb.pos[0] - (nb.entry[1] + 0.5), dy = nb.pos[1] - nb.entry[2];
                double dz = nb.pos[2] - (nb.entry[3] + 0.5);
                w->entry_d = sqrt(dx * dx + dy * dy + dz * dz);
            }
            /* the spawner the macro remembers (carried by a chain save) within CH_AT_SP of the feet */
            double dx = nb.pos[0] - (nb.sp[1] + 0.5), dy = nb.pos[1] - nb.sp[2], dz = nb.pos[2] - (nb.sp[3] + 0.5);
            w->at_sp = nb.sp[0] && dx * dx + dy * dy + dz * dz <= CH_AT_SP * CH_AT_SP;
            /* the way to the rods (lane/chain2): the spawner remembered, else the fortress cell */
            if (nb.sp[0]) w->goal_d = sqrt(dx * dx + dy * dy + dz * dz);
            else if (nb.fort[0])
                w->goal_d = hypot(nb.pos[0] - (nb.fort[1] + 0.5), nb.pos[2] - (nb.fort[3] + 0.5));
            /* the arrival portal's cell from the feet (the way home's progress) */
            if (nb.portal[0])
            {
                double px = nb.pos[0] - (nb.portal[1] + 0.5), py = nb.pos[1] - nb.portal[2], pz = nb.pos[2] - (nb.portal[3] + 0.5);
                w->home_d = sqrt(px * px + py * py + pz * pz);
            }
        }
    }
    if (w->p.dim != -1)
    {
        struct neth_vec_sh_book b;
        if (neth_vec_stronghold_book(S->v, i, 0, &b) == 0)
        {
            if (w->p.dims & 1) w->sh_room = b.room >= 0, w->sh_lit = b.lit >= 0, w->sh_filled = b.filled;
            w->st_room = b.room >= 0, w->st_lit = b.lit >= 0;
        }
    }
    if (w->p.dim == 1)
    {
        double d[NETH_VEC_DRAGON_INFO];
        if (neth_vec_dragon_info(S->v, i, d) == 0)
        {
            /* the lowest health the macro saw this segment, else the client's copy now (lane/chain2 tried the copy first:
             * the crystals heal it back over the rung's line a step later, so a gain whose save the busy slots refused
             * was gone; the lowest seen holds it until a slot frees) */
            w->dragon_hp = d[21] >= 0 ? d[21] : d[7];
            w->crystals = (int)(d[11] + d[73]);     /* crystals hit by arrows, lobs whose crystal went */
        }
    }
}

static void tsv_line(struct search *S, const struct node *n)
{
    char rn[64];
    rung_name(n->rung, rn, sizeof rn);
    const struct neth_rl_state *s = &n->p.st;
    fprintf(S->ftsv, "%d\t%d\t%d\t%s\t%.3f\t%lld\t%lld\t%.1f\t%.1f\t%d\t%d\t%.1f\t%.1f\t%.1f\t", n->id, n->parent,
            n->rung, rn, n->q, (long long)n->ticks, (long long)n->seg, n->wall, n->p.health, n->p.food, n->p.dim,
            n->p.x, n->p.y, n->p.z);
    int any = 0;
    for (int j = 0; j < 36; ++j)
        if (s->inv[j].item > 0 && s->inv[j].count > 0)
            fprintf(S->ftsv, "%s%d:%d", any++ ? "," : "", s->inv[j].item, s->inv[j].count);
    fprintf(S->ftsv, "\t%d\n", n->prog);
    fflush(S->ftsv);
}

/* a segment's file (SEG_MAGIC2): the header, its codec's sizes, then its steps as the recorder's packed columns
 * (rec_pack, the labelled data's codec) under zstd: about a tenth of the raw steps (SEG_MAGIC's, still read) */
struct seg_z { uint32_t nops, nexc; uint64_t raw, zlen; };

static int write_seg_path(const char *p, int id, int parent, const struct env *e, const struct neth_vec_chain_peek *st)
{
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    struct seg_head h = {SEG_MAGIC2, (uint32_t)sizeof(struct rec_step), id, parent, e->n, *st};
    struct rec_step *c = malloc((size_t)(e->n ? e->n : 1) * sizeof *c);
    memcpy(c, e->log, (size_t)e->n * sizeof *c);
    for (int64_t t = 0; t < e->n; ++t) rec_canon(&c[t]);
    uint8_t *pk = malloc(rec_packed_max((uint32_t)e->n));
    struct seg_z z = {0, 0, 0, 0};
    z.raw = rec_pack(c, (uint32_t)e->n, pk, &z.nops, &z.nexc);
    size_t zc = ZSTD_compressBound(z.raw);
    uint8_t *zb = malloc(zc);
    size_t zl = ZSTD_compress(zb, zc, pk, z.raw, 3);
    int ok = !ZSTD_isError(zl);
    z.zlen = ok ? zl : 0;
    ok = ok && fwrite(&h, sizeof h, 1, f) == 1 && fwrite(&z, sizeof z, 1, f) == 1 && fwrite(zb, 1, zl, f) == zl;
    free(c), free(pk), free(zb);
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int write_seg(struct search *S, const struct node *n, const struct env *e)
{
    char p[2200];
    snprintf(p, sizeof p, "%s/seg/%d.bin", S->dir, n->id);
    return write_seg_path(p, n->id, n->parent, e, &n->p);
}

static void frontier_table(struct search *S, double esps)
{
    char p[2200];
    snprintf(p, sizeof p, "%s/frontier.txt", S->dir);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "chain search %s: %.0f s, %lld segments, %.0f env-steps/s, %d nodes (%d keep their images), saves %lld "
               "(%lld failed, %lld refused: the save slots busy), let go %lld, deaths %lld; gains under %.0f health not "
               "saved: End %lld, elsewhere %lld\n",
            S->dir, now_s() - S->t0, (long long)S->segs, esps, S->nn, live_count(S, -1), (long long)S->saves,
            (long long)S->saves_failed, (long long)S->save_busy, (long long)S->freed, (long long)S->deaths,
            (double)CH_END_HP, (long long)S->floor_cut[0], (long long)S->floor_cut[1]);
    fprintf(f, "%-18s %6s %5s %7s %9s %10s %9s\n", "rung", "nodes", "live", "best_q", "tries", "first_at_s", "chain_min");
    for (int r = 0; r < R_N; ++r)
    {
        int nodes = 0, live = 0, tries = 0;
        float bq = -1;
        for (int k = 0; k < S->nn; ++k)
            if (S->nd[k].rung == r)
            {
                ++nodes;
                tries += S->nd[k].tries;
                if (S->nd[k].state == NS_READY && S->nd[k].start >= 0) ++live;
                if (S->nd[k].q > bq) bq = S->nd[k].q;
            }
        char rn[64];
        rung_name(r, rn, sizeof rn);
        if (!nodes)
        {
            fprintf(f, "%-18s %6d\n", rn, 0);
            continue;
        }
        /* (first_t -1: the rung's nodes are all still being made. lane/chain2: e44a's silent death at 946 s was
         * this line reading S->nd[-1], the page before the node array's mapping, kernel log 10:24:39 "chain segfault",
         * at the frontier write after a resumed node's long save) */
        fprintf(f, "%-18s %6d %5d %7.2f %9d %10.0f %9.1f  deaths %lld;", rn, nodes, live, bq, tries,
                S->first_t[r] >= 0 ? S->first_wall[r] : -1.0, S->first_t[r] >= 0 ? S->nd[S->first_t[r]].ticks / 1200.0 : -1.0,
                (long long)S->ddeath[r]);
        /* who drove the steps of the segments begun at this rung: the four largest */
        int64_t tot = 0;
        for (int d = 0; d < 33; ++d) tot += S->drive[r][d];
        int used[33] = {0};
        for (int q = 0; q < 4 && tot; ++q)
        {
            int bd = -1;
            for (int d = 0; d < 33; ++d)
                if (!used[d] && S->drive[r][d] && (bd < 0 || S->drive[r][d] > S->drive[r][bd])) bd = d;
            if (bd < 0) break;
            used[bd] = 1;
            const char *nm = bd == 0 ? "policy" : NULL;
            int32_t bit;
            for (int k = 0; bd && neth_vec_macro_switch(k, &bit); ++k)
                if (bit == (int32_t)(1u << (bd - 1))) nm = neth_vec_macro_switch(k, NULL);
            if (!nm && bd && (1u << (bd - 1)) == NETH_VEC_LBL_SCREEN) nm = "screen";
            fprintf(f, " %s %.0f%%", nm ? nm : "?", 100.0 * S->drive[r][bd] / tot);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "bucket portal (segments' ends, flint and steel or later): starts %lld, sites %lld, casts %lld, lit %lld, "
               "lava trips %lld, search steps %lld\n",
            (long long)S->bp[0], (long long)S->bp[1], (long long)S->bp[2], (long long)S->bp[3], (long long)S->bp[4],
            (long long)S->bp[5]);
    fprintf(f, "pearls (segments' ends, home to the twelfth): %lld played, endermen seen %lld, provoked %lld, killed %lld, "
               "pillars %lld, deaths %lld; steps by phase",
            (long long)S->pl[0], (long long)S->pl[1], (long long)S->pl[2], (long long)S->pl[3], (long long)S->pl[4],
            (long long)S->pl[5]);
    for (int q = 0; q < 12; ++q) fprintf(f, " %lld", (long long)S->pl[6 + q]);
    fprintf(f, "; oracle walking %lld, a pearl item near %lld\n", (long long)S->pl[18], (long long)S->pl[19]);
    static const char *const PDC[10] = {"enderman", "zombie", "skeleton", "creeper", "spider", "other mob", "burning",
                                        "drowning", "starving", "other"};
    fprintf(f, "pearls' deaths by cause:");
    for (int q = 0; q < 10; ++q) fprintf(f, " %s %lld", PDC[q], (long long)S->pl[20 + q]);
    fprintf(f, "\n");
    /* the first deaths by the hunt's phase (pearl.c PP_*: 0 walk 1 up 2 scan 3 down 4 drop 5 flee 6 guard 7 hide,
     * 11 pearl_home's leg; 12 the hunt not on), on the leg, wet the steps before */
    fprintf(f, "pearls' first deaths by phase:");
    for (int q = 0; q < 13; ++q) fprintf(f, " %d:%lld", q, (long long)S->pl[30 + q]);
    fprintf(f, "; on the leg %lld, wet before %lld; by the step's macro kind (vecenv.c M_*):", (long long)S->pl[43],
            (long long)S->pl[44]);
    for (int q = 0; q < 16; ++q)
        if (S->pl[45 + q]) fprintf(f, " %d:%lld", q, (long long)S->pl[45 + q]);
    fprintf(f, "\n");
    fprintf(f, "the End (segments from end on, in the End at their end): %lld played; deaths near the void %lld, the "
               "dragon %lld, an enderman %lld, else %lld; crystals taken by arrows %lld, by lobs %lld; the dragon macro "
               "off at the end %lld; sword hits %lld, arrows at the dragon %lld\n",
            (long long)S->eb[0], (long long)S->eb[1], (long long)S->eb[2], (long long)S->eb[3], (long long)S->eb[4],
            (long long)S->eb[5], (long long)S->eb[6], (long long)S->eb[7], (long long)S->eb[8], (long long)S->eb[9]);
    /* lane/chain2: the End's nodes keeping their images, then the last let go: the two scores' terms */
    fprintf(f, "the End's live nodes: id parent rung q prog tries gain_tries held pend pick_score evict_score "
               "server_crystals dragon_hp health armor_worn swords fresh_swords held_sword_wear\n");
    for (int k = 1; k < S->nn; ++k)
    {
        const struct node *n = &S->nd[k];
        if (n->rung < R_END || n->state != NS_READY || n->start < 0) continue;
        fprintf(f, "  live %d %d %d %.3f %d %d %d %d %d %.3f %.3f %d %.1f %.1f %d %d %d %d\n", n->id, n->parent, n->rung, n->q, n->prog,
                n->tries, n->gain_tries, n->held, n->pend, score_pick(n), score_evict(n), n->svc, n->dhp, n->p.health,
                n->armw, n->swords, n->fresh_swords, n->held_sword_wear);
    }
    for (int g = 0; g < CH_GONE && g < S->ngone; ++g)
    {
        const struct node *n = &S->gone[(S->ngone - 1 - g) % CH_GONE];
        fprintf(f, "  gone %d %d %d %.3f %d %d %d %d %d %.3f %.3f %d %.1f %.1f\n", n->id, n->parent, n->rung, n->q, n->prog,
                n->tries, n->gain_tries, n->held, n->pend, score_pick(n), S->gone_sc[(S->ngone - 1 - g) % CH_GONE], n->svc,
                n->dhp, n->p.health);
    }
    fprintf(f, "the End in detail: first deaths by cause (void, dragon, enderman, fall after a hit, fall, blast, arrow, "
               "starving, other)");
    for (int q = 0; q < 9; ++q) fprintf(f, " %lld", (long long)S->ed[q]);
    fprintf(f, "; pushed before %lld; at step under 3k %lld, 3k-10k %lld, 10k-25k %lld, over %lld; falls %lld blocks, food at "
               "death %lld summed; the dragon seen in %lld, lowest health at 150 or under %lld, 100 %lld, 50 %lld; health "
               "taken by swings %lld, by arrows %lld; boss bar rises %lld; the macro's phase at death",
            (long long)S->ed[9], (long long)S->ed[10], (long long)S->ed[11], (long long)S->ed[12], (long long)S->ed[13],
            (long long)S->ed[14], (long long)S->ed[15], (long long)S->ed[19], (long long)S->ed[16], (long long)S->ed[17],
            (long long)S->ed[18], (long long)S->ed[20], (long long)S->ed[21], (long long)S->ed[22]);
    for (int q = 0; q < 14; ++q) fprintf(f, " %lld", (long long)S->ed[23 + q]);
    fprintf(f, " (none, climb, seek, shoot, fight, exit, eat, fetch, pit, snipe, descend, hunt, follow, raid); falls with a "
               "length %lld, of them from y 68 or over %lld\n", (long long)S->ed[37], (long long)S->ed[38]);
    fprintf(f, "pearls' provoked locks, how they ended: killed %lld, the player dead %lld, let go by day %lld, let go at "
               "night %lld, another taken %lld\n",
            (long long)S->pl[61], (long long)S->pl[62], (long long)S->pl[63], (long long)S->pl[64],
            (long long)S->pl[65]);
    fprintf(f, "way home (segments from rod_7): %lld played, %lld died in the Nether: burning %lld, not %lld; from the "
               "spawner under 8 %lld, 8-20 %lld, 20-60 %lld, 60+ %lld, unknown %lld; within 10 of the portal %lld\n",
            (long long)S->hm[0], (long long)S->hm[1], (long long)S->hm[3], (long long)S->hm[5], (long long)S->hm[8], (long long)S->hm[9],
            (long long)S->hm[10], (long long)S->hm[11], (long long)S->hm[12], (long long)S->hm[13]);
    fprintf(f, "food (segments' ends): scouts %lld, found an animal %lld, ended out of time %lld, below the surface %lld, "
               "water %lld, night or held off %lld, cut short %lld; hunts %lld, meat got %lld, hunts ended with meat "
               "%lld, lost %lld, out of time %lld\n",
            (long long)S->fb[0], (long long)S->fb[1], (long long)S->fb[2], (long long)S->fb[3], (long long)S->fb[4],
            (long long)S->fb[5], (long long)S->fb[6], (long long)S->fb[7], (long long)S->fb[8], (long long)S->fb[9],
            (long long)S->fb[10], (long long)S->fb[11]);
    fclose(f);
}

static int top_rung(const struct search *S)
{
    int t = 0;
    for (int k = 0; k < S->nn; ++k)
        if (S->nd[k].state != NS_FAILED && !S->nd[k].bad && S->nd[k].rung > t) t = S->nd[k].rung;
    return t;
}

/* the driver's keys never held (bit j: key j; vecenv.c decode's KEYS: forward back left right sprint jump sneak mine
 * place throw drop hit swap_item paused): place, throw, drop, swap_item and paused, so a random step never drops the
 * held tool, pours the water bucket or lights the flint and steel; the macros use them themselves */
#define CH_KEYS_OFF ((1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 13))
static void env_policy(int32_t *a, int aw, uint64_t *rng, int camw, int keyp, uint32_t off)
{
    memset(a, 0, (size_t)aw * sizeof *a);
    for (int j = 0; j < 2; ++j) a[j] = NETH_VEC_CAM / 2 - camw + (int)(xs(rng) % (uint64_t)(2 * camw + 1));
    for (int j = 0; j < NETH_VEC_KEYS; ++j) a[2 + j] = (int)(xs(rng) & 1023) < keyp && !(off >> j & 1);
}

/* --chooser FILE (lane/chain3, with --rollout-out only; off by default): a pick table in place of the random driver.
 * FILE's lines (TAB, '#' a comment): bucket rung name, the previous step's driver kind (rec_step.kind: 0 the policy,
 * else the macro kind that drove it), the step's kind, the camera's two bins, the 14 keys as 0/1 and a count: the
 * policy steps of proved kills' labelled episodes, each labelled by its Java row's rung. Each rollout step's action is
 * one of those steps drawn by count among the env's bucket (ch_bucket of its best rung) and its last step's kind, so
 * P(kind | rung) and the actions under each come from the kills together (the kind column only reports them). A
 * (bucket, kind) with no steps falls back to (bucket, policy), then to the nearest lower bucket, then to the random
 * driver; the counts of each are printed at the end. */
#define CH_KINDS 8
struct chooser {
    int64_t m[R_N][CH_KINDS], cap[R_N][CH_KINDS];
    uint32_t *a[R_N][CH_KINDS];     /* cam0 | cam1 << 8 | keys << 16 */
    int64_t *cum[R_N][CH_KINDS];    /* cumulative counts */
    int64_t used[4];                /* steps drawn: exact, (bucket, policy), a lower bucket, the random driver */
};

/* the bucket of a rung: the Nether's sub-rungs, the room and the End's dragon rungs have no reading on the Java row
 * (the macros' books), so the table holds them under their leg's first rung */
static int ch_bucket(int r)
{
    if (r == R_FORT_SEEN || r == R_ON_FORT || r == R_AT_SP) return R_NETHER;
    if (r == R_SH_ROOM || r == R_FILLED) return R_SNOW;
    if (r >= R_D75) return R_END;
    return r;
}

static int chooser_read(const char *path, struct chooser *c)
{
    FILE *f = fopen(path, "r");
    if (!f) return fprintf(stderr, "chain: --chooser %s: %s\n", path, strerror(errno)), -1;
    char line[512], name[64], keys[64];
    int pk, k, c0, c1, ln = 0;
    long long cnt;
    while (fgets(line, sizeof line, f))
    {
        ++ln;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%63s\t%d\t%d\t%d\t%d\t%63s\t%lld", name, &pk, &k, &c0, &c1, keys, &cnt) != 7 ||
            strlen(keys) != NETH_VEC_KEYS || pk < 0 || pk >= CH_KINDS || c0 < 0 || c0 >= NETH_VEC_CAM || c1 < 0 ||
            c1 >= NETH_VEC_CAM || cnt <= 0)
        {
            fclose(f);
            return fprintf(stderr, "chain: --chooser %s:%d: not bucket, kind, kind, cam, cam, keys, count\n", path, ln), -1;
        }
        int r = rung_by_name(name);
        if (r < 0 || ch_bucket(r) != r)
        {
            fclose(f);
            return fprintf(stderr, "chain: --chooser %s:%d: %s is not a bucket rung\n", path, ln, name), -1;
        }
        uint32_t kb = 0;
        for (int j = 0; j < NETH_VEC_KEYS; ++j) kb |= (uint32_t)(keys[j] == '1') << j;
        if (c->m[r][pk] == c->cap[r][pk])
        {
            c->cap[r][pk] = c->cap[r][pk] ? 2 * c->cap[r][pk] : 256;
            c->a[r][pk] = realloc(c->a[r][pk], (size_t)c->cap[r][pk] * sizeof *c->a[r][pk]);
            c->cum[r][pk] = realloc(c->cum[r][pk], (size_t)c->cap[r][pk] * sizeof *c->cum[r][pk]);
        }
        int64_t j = c->m[r][pk]++;
        c->a[r][pk][j] = (uint32_t)c0 | (uint32_t)c1 << 8 | kb << 16;
        c->cum[r][pk][j] = (j ? c->cum[r][pk][j - 1] : 0) + cnt;
    }
    fclose(f);
    return 0;
}

static void chooser_act(struct chooser *c, int rung, int prev, int32_t *a, int aw, uint64_t *rng, int camw, int keyp,
                        uint32_t off)
{
    int b = ch_bucket(rung), pk = prev >= 0 && prev < CH_KINDS ? prev : 0, how = 0;
    if (!c->m[b][pk]) pk = 0, how = 1;
    while (b > 0 && !c->m[b][pk]) b = ch_bucket(b - 1), how = 2;
    if (!c->m[b][pk])
    {
        c->used[3]++;
        env_policy(a, aw, rng, camw, keyp, off);
        return;
    }
    c->used[how]++;
    int64_t n = c->m[b][pk], x = (int64_t)(xs(rng) % (uint64_t)c->cum[b][pk][n - 1]), lo = 0, hi = n - 1;
    while (lo < hi)
    {
        int64_t mid = (lo + hi) / 2;
        if (c->cum[b][pk][mid] > x) hi = mid;
        else lo = mid + 1;
    }
    uint32_t v = c->a[b][pk][lo];
    memset(a, 0, (size_t)aw * sizeof *a);
    a[0] = (int32_t)(v & 255u), a[1] = (int32_t)(v >> 8 & 255u);
    for (int j = 0; j < NETH_VEC_KEYS; ++j) a[2 + j] = (int)(v >> (16 + j) & 1u) && !(off >> j & 1);
}

static int cmd_search(int argc, char **argv)
{
    const char *run = NULL, *dir = NULL, *mopt[CH_MOPT_MAX], *resume = NULL, *resume_bad = NULL, *resume_ids = NULL;
    const char *trace_path = NULL, *death_log_path = NULL, *end_log_path = NULL;
    FILE *end_out = NULL;    /* --end-log */
    const char *etrace_arg = NULL;
    FILE *etrace_out = NULL; /* --end-trace I:FILE */
    int etrace_i = -1;
    int trace_steps = 0, trace_env = -1, trace_every = 200;
    const char *segment_log = NULL, *rollout_path = NULL, *chooser_path = NULL, *replay_leg = NULL, *replay_script = NULL, *tape_attempt = NULL;
    FILE *tape_f = NULL;     /* --tape-attempt */
    const char *stage_path = NULL, *cross_dir = NULL, *replay_steps = NULL, *replay_mem = NULL;  /* lane/fwd */
    FILE *stage_out = NULL;
    /* lane/choice: --choice-log FILE, --replay-reseed S, --mem-at N:FILE (all off by default; books only but the reseed,
     * which draws the restore's live part from sampler S) */
    const char *choice_path = NULL, *replay_reseed = NULL, *mem_at = NULL;
    FILE *choice_out = NULL;
    int64_t mem_at_n = -1;
    char mem_at_file[2200] = "";
    const char *wood_path = NULL;   /* lane/wood */
    FILE *wood_out = NULL;
    const char *lvlog_path = NULL;  /* lane/lava: --lava-log FILE */
    FILE *lvlog_out = NULL;
    int tape_i = -1;
    struct neth_rl_act *leg_acts = NULL;
    int64_t leg_n = 0;
    int replay_lead = 0;    /* --replay-lead N: no acts before the leg's first segment (its reset step's alignment) */
    int start_latched = 0;  /* --start-latched 1: the run's start passes the chain's latches (a library start) */
    int dev_start = 0;      /* --dev 1 (lane/rods): a dev start taken (NETH_RL_DEV: its header's dev ops at tick 0); a
                             * what-if's rows only, never a benchmark's */
    int etrace_hunt = 0;    /* --end-trace-hunt 1 (lane/legs): the End trace's rows add the crystal hunt's columns */
    int etrace_ender = 0;   /* --end-trace-ender 1 (lane/dleg): ... and the census's endermen (PRIVILEGED, read only) */
    const char *census_path = NULL;  /* --owner-census FILE (lane/legs): each attempt's steps by owning macro and kind */
    FILE *census_out = NULL;
    const char *ntrace_arg = NULL;   /* --nether-trace I:FILE (lane/legs): attempt I's (-1 every attempt's) Nether steps */
    FILE *ntrace_out = NULL, *rodlog_out = NULL;
    int ntrace_i = -1;
    const char *shtrace_arg = NULL;  /* --shold-trace I[/K]:FILE (lane/proom): attempt I's (-1 every attempt's)
                                      * overworld steps, every K-th (1), the stronghold latch and its holds */
    FILE *shtrace_out = NULL;
    int shtrace_i = -1, shtrace_k = 1;
    const char *build_path = NULL;   /* --build-log FILE (lane/bportal): a row an attempt, the portal build's steps */
    FILE *build_out = NULL;
    const char *supply_path = NULL;  /* --supply-log FILE (lane/supply): the holdings at the Nether entry and the end */
    FILE *supply_out = NULL;
    const char *ladder_path = NULL;  /* --ladder-log FILE (lane/dleg): a row an attempt, the End's rungs and books */
    FILE *ladder_out = NULL;
    int ld_stage[LAD_RUNGS] = {0};
    const char *ptrace_arg = NULL;   /* --portal-trace I[/K]:FILE (lane/bportal): attempt I's (-1 every attempt's)
                                      * overworld steps, every K-th (1): the build's items and each macro's start test */
    FILE *ptrace_out = NULL;
    int ptrace_i = -1, ptrace_k = 1;
    int target_stage = -1, stage_base = 0;  /* --target-stage NAME[:BASE] (lane/legs): stage_hit ends an attempt */
    /* --target-gain [KIND:]N: an attempt reaches at N more of KIND than it began with: pearl (lane/proom: pearls and
     * eyes, its hunt books; a bare N) or rod (lane/rods: rods, powder and eyes as rods) */
    int target_gain = 0, gain_kind = TG_PEARL;
    const char *gain_path = NULL;            /* --gain-log FILE (lane/rods) */
    FILE *gain_out = NULL;
    const char *hunt_path = NULL;    /* --hunt-log FILE (lane/proom): a row an attempt, the pearl hunt's end */
    FILE *hunt_out = NULL;
    const char *dtrace_path = NULL;  /* --death-trace FILE (lane/proom): each dead attempt's last DT_RING steps */
    FILE *dtrace_out = NULL;
    const char *ktrace_path = NULL;  /* --kill-trace FILE (lane/pground): each enderman kill's last DT_RING steps */
    FILE *ktrace_out = NULL;
    const char *nlog_path = NULL;    /* --night-log FILE (lane/pnight): a row an attempt a night, at each dawn and the end */
    FILE *nlog_out = NULL;
    const char *dlog_path = NULL;    /* --dig-log FILE (lane/pdig): the endermen within 48, a row a visit and an attempt */
    FILE *dlog_out = NULL;
    const char *slog_path = NULL;    /* --sight-log FILE (lane/pdig): a row an enderman seen in view, how it ended */
    FILE *slog_out = NULL;
    const char *flog_path = NULL;    /* --floor-log FILE (lane/pground): a row an attempt a half day, the floor box's */
    FILE *flog_out = NULL;
    int32_t fl_box[6] = {0};
    int fl_box_set = 0;
    int n = 0, groups = 0, threads = 0, nm = 0, target = R_DEAD, resume_k = 16, resume_max = R_N - 1;
    int single_segment = 0, fresh_memory = 0, resume_next = 0;
    int hold_on = 1, save_slots = 0, force_save_at = 0;
    double seg_s = CH_SEG_S, minutes = 0, cam_w = CH_CAM_W, key_p = CH_KEY_P, every = 30;
    uint64_t seed = 1;
    uint32_t keys_off = CH_KEYS_OFF;
    struct search S;
    memset(&S, 0, sizeof S);
    S.keep = CH_KEEP, S.live = CH_LIVE, S.free_max = CH_FREE, S.save_wait = CH_SAVE_WAIT;
    S.snow_min = CH_SNOW;
    for (int i = 2; i < argc; ++i)
    {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v) return fprintf(stderr, "chain search: %s needs a value\n", a), 2;
        ++i;
        if (!strcmp(a, "--run")) run = v;
        else if (!strcmp(a, "--dir")) dir = v;
        else if (!strcmp(a, "--n")) n = atoi(v);
        else if (!strcmp(a, "--groups")) groups = atoi(v);
        else if (!strcmp(a, "--threads")) threads = atoi(v);
        else if (!strcmp(a, "--seg")) seg_s = atof(v);
        else if (!strcmp(a, "--water-seg")) S.water_seg = (int64_t)llround(atof(v) * 20);
        else if (!strcmp(a, "--free")) S.free_max = atoi(v);
        else if (!strcmp(a, "--keep")) S.keep = atoi(v);
        else if (!strcmp(a, "--cell")) S.cell = atoi(v);
        else if (!strcmp(a, "--live")) S.live = atoi(v);
        else if (!strcmp(a, "--save-slots")) save_slots = atoi(v);
        else if (!strcmp(a, "--minutes")) minutes = atof(v);
        else if (!strcmp(a, "--target")) target = rung_by_name(v);
        else if (!strcmp(a, "--target-stage"))
        {
            char nm_[64];
            snprintf(nm_, sizeof nm_, "%s", v);
            char *colon = strchr(nm_, ':');
            if (colon) *colon = 0, stage_base = atoi(colon + 1);
            target_stage = stage_by_name(nm_);
            if (target_stage <= 0) return fprintf(stderr, "chain search: --target-stage %s: no such stage\n", v), 2;
        }
        else if (!strcmp(a, "--target-gain"))
        {
            const char *num = v, *colon = strchr(v, ':');
            if (colon)
            {
                size_t k = (size_t)(colon - v);
                if (k == 3 && !strncmp(v, "rod", 3)) gain_kind = TG_ROD;
                else if (k == 5 && !strncmp(v, "pearl", 5)) gain_kind = TG_PEARL;
                else if (k == 3 && !strncmp(v, "obs", 3)) gain_kind = TG_OBS;
                else if (k == 5 && !strncmp(v, "armor", 5)) gain_kind = TG_ARMOR;
                else return fprintf(stderr, "chain search: --target-gain %s: KIND is rod, pearl, obs or armor\n", v), 2;
                num = colon + 1;
            }
            target_gain = atoi(num);
        }
        else if (!strcmp(a, "--gain-log")) gain_path = v;
        else if (!strcmp(a, "--cam")) cam_w = atof(v);
        else if (!strcmp(a, "--keys")) key_p = atof(v);
        else if (!strcmp(a, "--keys-off")) keys_off = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--save-wait")) S.save_wait = atoi(v);
        else if (!strcmp(a, "--force-save-at")) force_save_at = atoi(v);
        else if (!strcmp(a, "--every")) every = atof(v);
        else if (!strcmp(a, "--mopt"))
        {
            if (nm == CH_MOPT_MAX) return fprintf(stderr, "chain search: over %d --mopt\n", CH_MOPT_MAX), 2;
            mopt[nm++] = v;
        }
        else if (!strcmp(a, "--resume")) resume = v;
        else if (!strcmp(a, "--resume-k")) resume_k = atoi(v);
        else if (!strcmp(a, "--resume-max")) resume_max = rung_by_name(v);
        else if (!strcmp(a, "--resume-bad")) resume_bad = v;
        else if (!strcmp(a, "--resume-ids")) resume_ids = v;
        else if (!strcmp(a, "--resume-next")) resume_next = atoi(v);
        else if (!strcmp(a, "--fresh-memory")) fresh_memory = atoi(v);
        else if (!strcmp(a, "--segment-log")) segment_log = v;
        else if (!strcmp(a, "--trace")) trace_path = v;
        else if (!strcmp(a, "--death-log")) death_log_path = v;
        else if (!strcmp(a, "--end-log")) end_log_path = v;
        else if (!strcmp(a, "--end-trace")) etrace_arg = v;
        else if (!strcmp(a, "--end-trace-hunt")) etrace_hunt = atoi(v);
        else if (!strcmp(a, "--end-trace-ender")) etrace_ender = atoi(v);
        else if (!strcmp(a, "--owner-census")) census_path = v;
        else if (!strcmp(a, "--hunt-log")) hunt_path = v;
        else if (!strcmp(a, "--death-trace")) dtrace_path = v;
        else if (!strcmp(a, "--kill-trace")) ktrace_path = v;
        else if (!strcmp(a, "--lava-log")) lvlog_path = v;
        else if (!strcmp(a, "--night-log")) nlog_path = v;
        else if (!strcmp(a, "--dig-log")) dlog_path = v;
        else if (!strcmp(a, "--floor-log")) flog_path = v;
        else if (!strcmp(a, "--floor-box"))
        {
            if (sscanf(v, "%d,%d,%d,%d,%d,%d", &fl_box[0], &fl_box[1], &fl_box[2], &fl_box[3], &fl_box[4], &fl_box[5]) != 6 ||
                fl_box[3] < fl_box[0] || fl_box[4] < fl_box[1] || fl_box[5] < fl_box[2])
                return fprintf(stderr, "chain: --floor-box X0,Y0,Z0,X1,Y1,Z1 (cells, inclusive)\n"), 2;
            fl_box_set = 1;
        }
        else if (!strcmp(a, "--sight-log")) slog_path = v;
        else if (!strcmp(a, "--nether-trace")) ntrace_arg = v;
        else if (!strcmp(a, "--shold-trace")) shtrace_arg = v;
        else if (!strcmp(a, "--build-log")) build_path = v;
        else if (!strcmp(a, "--supply-log")) supply_path = v;
        else if (!strcmp(a, "--ladder-log")) ladder_path = v;
        else if (!strcmp(a, "--portal-trace")) ptrace_arg = v;
        else if (!strcmp(a, "--trace-steps")) trace_steps = atoi(v);
        else if (!strcmp(a, "--trace-env")) trace_env = atoi(v);
        else if (!strcmp(a, "--trace-every")) trace_every = atoi(v);
        else if (!strcmp(a, "--single-segment")) single_segment = atoi(v);
        else if (!strcmp(a, "--rollout-out")) rollout_path = v;
        else if (!strcmp(a, "--chooser")) chooser_path = v;
        else if (!strcmp(a, "--replay-leg")) replay_leg = v;
        else if (!strcmp(a, "--replay-script")) replay_script = v;
        else if (!strcmp(a, "--tape-attempt")) tape_attempt = v;
        else if (!strcmp(a, "--stage-log")) stage_path = v;
        else if (!strcmp(a, "--wood-log")) wood_path = v;
        else if (!strcmp(a, "--crossings")) cross_dir = v;
        else if (!strcmp(a, "--replay-steps")) replay_steps = v;
        else if (!strcmp(a, "--replay-mem")) replay_mem = v;
        else if (!strcmp(a, "--replay-reseed")) replay_reseed = v;
        else if (!strcmp(a, "--choice-log")) choice_path = v;
        else if (!strcmp(a, "--mem-at")) mem_at = v;
        else if (!strcmp(a, "--replay-lead")) replay_lead = atoi(v);
        else if (!strcmp(a, "--start-latched")) start_latched = atoi(v);
        else if (!strcmp(a, "--dev")) dev_start = atoi(v);
        else if (!strcmp(a, "--provisions")) S.prov = atoi(v);
        else if (!strcmp(a, "--meat")) S.meat = atoi(v);
        else if (!strcmp(a, "--carry")) S.carry = atoi(v);
        else if (!strcmp(a, "--dayff")) S.dayff = atoi(v);
        else if (!strcmp(a, "--spent")) S.spent = atoi(v);
        else if (!strcmp(a, "--late-seg")) S.late_seg = (int64_t)llround(atof(v) * 20);
        else if (!strcmp(a, "--land-seg")) S.land_seg = (int64_t)llround(atof(v) * 20);
        else if (!strcmp(a, "--snow-seg")) S.snow_seg = (int64_t)llround(atof(v) * 20);
        else if (!strcmp(a, "--snow-min"))
        {
            S.snow_min = atoi(v);
            if (S.snow_min != 16 && S.snow_min != 32 && S.snow_min != 64)
                return fprintf(stderr, "chain search: --snow-min must be 16, 32 or 64\n"), 2;
        }
        else if (!strcmp(a, "--kit")) S.kit = atoi(v);
        else if (!strcmp(a, "--armor-min")) S.armor_min = atoi(v);
        else if (!strcmp(a, "--obs-health-min")) S.obs_health_min = atoi(v);
        else if (!strcmp(a, "--obs-food-min")) S.obs_food_min = atoi(v);
        else if (!strcmp(a, "--endfood")) S.endfood = atoi(v);
        else if (!strcmp(a, "--post-stock-food")) S.post_stock_food = atoi(v);
        else if (!strcmp(a, "--late-health-min")) S.late_health_min = atoi(v);
        else if (!strcmp(a, "--late-food-min")) S.late_food_min = atoi(v);
        else if (!strcmp(a, "--end-fed")) S.end_fed = atoi(v);
        else if (!strcmp(a, "--end-armor")) S.end_armor = atoi(v);
        else if (!strcmp(a, "--hold")) hold_on = atoi(v);
        else if (!strcmp(a, "--min-rung")) S.min_rung = rung_by_name(v) < 0 ? 0 : rung_by_name(v);
        else if (!strcmp(a, "--end-seg")) S.end_seg = atoll(v);
        else if (!strcmp(a, "--toward")) S.toward = !strcmp(v, "locate") ? 2 : sscanf(v, "%lf,%lf", &S.tx, &S.tz) == 2;
        else if (!strcmp(a, "--room-score-macro")) S.room_score_macro = atoi(v);
        else if (!strcmp(a, "--room-score-prospective")) S.room_score_prospective = atoi(v);
        else if (!strcmp(a, "--bad")) snprintf(S.bad_path, sizeof S.bad_path, "%s", v);
        else return fprintf(stderr, "chain search: unknown %s\n", a), 2;
    }
    snow_name_min = S.snow_min;
    if (!run || !dir || target < 0) return fprintf(stderr, "chain search: --run FILE --dir DIR (and a known --target)\n"), 2;
    if (resume_next != 0 && (resume_next != 1 || !resume))
        return fprintf(stderr, "chain search: --resume-next 1 requires --resume\n"), 2;
    if ((S.room_score_macro != 0 && S.room_score_macro != 1) || (S.room_score_macro && !S.toward))
        return fprintf(stderr, "chain search: --room-score-macro needs 0 or 1 and --toward\n"), 2;
    if ((S.room_score_prospective != 0 && S.room_score_prospective != 1) ||
        (S.room_score_prospective && !S.room_score_macro))
        return fprintf(stderr, "chain search: --room-score-prospective needs 0 or 1 and --room-score-macro 1\n"), 2;
    if (!!trace_path != (trace_steps > 0) || trace_every < 1 || trace_env < -1 ||
        (trace_path && (trace_env < 0 ? n != 1 : trace_env >= n)) || (!trace_path && trace_env >= 0))
        return fprintf(stderr, "chain search: --trace FILE needs --trace-steps N and --n 1 or --trace-env ID\n"), 2;
    if (single_segment && (n != 1 || !resume))
        return fprintf(stderr, "chain search: --single-segment needs --n 1 and --resume DIR\n"), 2;
    if (fresh_memory != 0 && fresh_memory != 1)
        return fprintf(stderr, "chain search: --fresh-memory needs 0 or 1\n"), 2;
    for (int k = 0; k < nm; ++k)
        if (!strcmp(mopt[k], "nether_chamber=stronghold")) S.chamber_stronghold = 1;
    if (!S.armor_min) S.armor_min = CH_ARMOR;
    if (S.armor_min < CH_ARMOR || S.armor_min > 15)
        return fprintf(stderr, "chain search: --armor-min must be 13 to 15\n"), 2;
    if (S.late_health_min < 0 || S.late_health_min > 20 || S.late_food_min < 0 || S.late_food_min > 1024)
        return fprintf(stderr, "chain search: late health must be 0..20 and held food points 0..1024\n"), 2;
    if (S.post_stock_food < 0 || (S.post_stock_food && (S.endfood <= 0 || S.post_stock_food > S.endfood)))
        return fprintf(stderr, "chain search: --post-stock-food needs 0 or a positive floor at most --endfood\n"), 2;
    if (S.water_seg < 0 || S.water_seg > 36000)
        return fprintf(stderr, "chain search: --water-seg must be 0..1800 game seconds\n"), 2;
    if (S.land_seg < 0 || S.land_seg > 36000)
        return fprintf(stderr, "chain search: --land-seg must be 0..1800 game seconds\n"), 2;
    struct rlimit nof;
    if (getrlimit(RLIMIT_NOFILE, &nof) == 0 && nof.rlim_cur < nof.rlim_max)
    {
        nof.rlim_cur = nof.rlim_max;
        setrlimit(RLIMIT_NOFILE, &nof);
    }
    struct runf rf;
    memset(&rf, 0, sizeof rf);
    if (read_run(run, &rf)) return 2;
    int macros = macro_set(rf.macro_spec);
    if (macros < 0) return 2;
    for (int k = 0; k < nm; ++k)
    {
        const char *eq = strchr(mopt[k], '=');
        if (!eq) return fprintf(stderr, "chain: --mopt %s is not NAME=WORD\n", mopt[k]), 2;
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(eq - mopt[k]), mopt[k]);
        if (mopt_add(rf.mopts, sizeof rf.mopts, name, eq + 1) < 0)
            return fprintf(stderr, "chain: macro options exceed %zu bytes\n", sizeof rf.mopts - 1), 2;
    }
    S.provision_on = strstr(rf.mopts, "food_provision=on") != NULL ||
                     strstr(rf.mopts, "food_provision=armor_first") != NULL;
    if (!n) n = CH_N;
    if (!groups) groups = CH_GROUPS;
    if (n % groups) return fprintf(stderr, "chain: N %d over %d groups\n", n, groups), 2;
    if (groups > 256) return fprintf(stderr, "chain: over 256 groups\n"), 2;
    if (rollout_path && (groups != n || single_segment || seg_s <= 0 || n > 256 ||
                         (resume ? (!resume_ids || strchr(resume_ids, ',') || resume_k != 1) : !!resume_ids)))
        return fprintf(stderr, "chain: --rollout-out needs --groups N and a positive --seg bound; "
                               "with --resume also give one --resume-ids node and --resume-k 1\n"), 2;
    if (chooser_path && !rollout_path) return fprintf(stderr, "chain: --chooser needs --rollout-out\n"), 2;
    struct chooser *chooser = NULL;
    if (chooser_path)
    {
        chooser = calloc(1, sizeof *chooser);
        if (chooser_read(chooser_path, chooser)) return 2;
    }
    if (start_latched && (!rollout_path || resume))
        return fprintf(stderr, "chain: --start-latched needs --rollout-out and no --resume\n"), 2;
    if (replay_leg)
    {
        /* --replay-leg DIR:NODE (lane/chain3, the known-answer check of a stage leg): every attempt plays the proved
         * path's recorded acts from the --resume-ids source to NODE (each later seam's reset as a no act, as --resume
         * plays a chain), the rung read as for any attempt; it should reach --target at NODE's tick less the source's */
        char ld[2200];
        snprintf(ld, sizeof ld, "%s", replay_leg);
        char *colon = strrchr(ld, ':');
        if (!rollout_path || !resume || !colon || chooser_path)
            return fprintf(stderr, "chain: --replay-leg DIR:NODE needs --rollout-out and --resume, not --chooser\n"), 2;
        *colon = 0;
        int lnode = atoi(colon + 1), src = atoi(resume_ids);
        struct segf *c;
        int ns = chain_load(ld, lnode, &c);
        if (ns < 0) return 1;
        int at = -1;
        for (int q = 0; q < ns; ++q)
            if (c[q].h.parent == src) at = q;
        if (at < 0) return fprintf(stderr, "chain: --replay-leg: node %d is not on %d's path in %s\n", src, lnode, ld), 2;
        for (int q = at; q < ns; ++q) leg_n += c[q].h.n + (q > at ? 1 : replay_lead);
        leg_acts = calloc((size_t)(leg_n ? leg_n : 1), sizeof *leg_acts);
        int64_t t = 0;
        for (int q = at; q < ns; ++q)
        {
            for (int k = 0; k < (q > at ? 1 : replay_lead); ++k)
            {
                leg_acts[t].hotbar = -1;   /* (calloc'd: the resume's seam act, all else zero) */
                ++t;
            }
            for (int64_t u = 0; u < c[q].h.n; ++u) leg_acts[t++] = c[q].s[u].act;
        }
        for (int q = 0; q < ns; ++q) free(c[q].s);
        free(c);
        printf("replay-leg: %lld acts from node %d to node %d of %s\n", (long long)leg_n, src, lnode, ld);
    }
    if (replay_script)
    {
        /* --replay-script FILE:STEP (lane/chain3, the known-answer check of a library start): every attempt plays an
         * oracle script's acts (chain stitch's form: a Snapshot line, then a line a world tick) from its STEP-th act on,
         * the rung read as for any attempt; a library start harvested at tick STEP of that script should reach --target
         * at the kill's stage tick less STEP */
        char sf[2200];
        snprintf(sf, sizeof sf, "%s", replay_script);
        char *colon = strrchr(sf, ':');
        if (!rollout_path || resume || replay_leg || chooser_path || !colon)
            return fprintf(stderr, "chain: --replay-script FILE:STEP needs --rollout-out, not --resume, --replay-leg or --chooser\n"), 2;
        *colon = 0;
        int64_t from = atoll(colon + 1), k = 0, cap = 1 << 16;
        FILE *f = fopen(sf, "r");
        if (!f) return fprintf(stderr, "chain: %s: %s\n", sf, strerror(errno)), 1;
        leg_acts = calloc((size_t)cap, sizeof *leg_acts);
        static const char *const kn[NETH_RL_KEYS] = {"forward", "back", "left", "right", "jump", "sneak", "sprint", "attack",
                                                  "use", "drop", "inventory", "pick", "hotbar.1", "hotbar.2", "hotbar.3",
                                                  "hotbar.4", "hotbar.5", "hotbar.6", "hotbar.7", "hotbar.8", "hotbar.9"};
        char *line = NULL;
        size_t lcap = 0;
        while (getline(&line, &lcap, f) > 0)
        {
            if (strncmp(line, "{\"n\":", 5)) continue;   /* (the Snapshot command) */
            int reps = atoi(line + 5);
            struct neth_rl_act a;
            memset(&a, 0, sizeof a);
            a.hotbar = -1;
            const char *p;
            if ((p = strstr(line, "\"look\":[")) || (p = strstr(line, "\"dlook\":[")))
            {
                a.look_mode = p[1] == 'l' ? 1 : 2;
                p = strchr(p, '[') + 1;
                char *q;
                a.look[0] = strtof(p, &q);
                a.look[1] = strtof(q + 1, NULL);
            }
            for (int w = 0; w < 2; ++w)
                if ((p = strstr(line, w ? "\"press\":[" : "\"hold\":[")))
                {
                    const char *end = strchr(p, ']');
                    for (p = strchr(p, '['); p && p < end && (p = strchr(p, '"')) && p < end;)
                    {
                        const char *q = strchr(p + 1, '"');
                        int key = -1;
                        for (int j = 0; j < NETH_RL_KEYS; ++j)
                            if ((size_t)(q - p - 1) == strlen(kn[j]) && !strncmp(p + 1, kn[j], (size_t)(q - p - 1))) key = j;
                        if (key < 0) return fprintf(stderr, "chain: --replay-script: key %.*s\n", (int)(q - p - 1), p + 1), 2;
                        if (w) a.press[key]++;
                        else a.hold |= 1u << key;
                        p = q + 1;
                    }
                }
            if ((p = strstr(line, "\"hotbar\":"))) a.hotbar = atoi(p + 9);
            if ((p = strstr(line, "\"gui\":[")))
            {
                p += 7;
                while (*p == '[' && a.nops < NETH_RL_OPS)
                {
                    struct neth_rl_op *o = &a.ops[a.nops++];
                    int x[4] = {0, 0, 0, 0};
                    if (!strncmp(p, "[\"click\",", 9)) o->kind = PG_CLICK, sscanf(p + 9, "%d,%d,%d,%d", &x[0], &x[1], &x[2], &x[3]);
                    else if (!strncmp(p, "[\"close\"", 8)) o->kind = PG_CLOSE;
                    else if (!strncmp(p, "[\"respawn\"", 10)) o->kind = PG_RESPAWN;
                    else if (!strncmp(p, "[\"wake\"", 7)) o->kind = PG_WAKE;
                    else return fprintf(stderr, "chain: --replay-script: gui op %.20s\n", p), 2;
                    o->window = x[0], o->slot = x[1], o->button = x[2], o->mode = x[3];
                    p = strchr(p, ']') + 1;
                    if (*p == ',') ++p;
                }
            }
            if (strstr(line, "\"ctrl\":") || strstr(line, "\"aim\"")) return fprintf(stderr, "chain: --replay-script: an act field it does not take\n"), 2;
            for (int r = 0; r < (reps > 0 ? reps : 1); ++r, ++k)
            {
                if (k < from) continue;
                if (leg_n == cap) leg_acts = realloc(leg_acts, (size_t)(cap *= 2) * sizeof *leg_acts);
                leg_acts[leg_n++] = a;
            }
        }
        free(line);
        fclose(f);
        printf("replay-script: %lld acts from act %lld of %s\n", (long long)leg_n, (long long)from, sf);
    }
    /* lane/fwd: --stage-log, --crossings, --replay-steps and --replay-mem (all off by default, books only but the
     * restore, which plays acts as --replay-script does) */
    if ((stage_path || cross_dir || replay_steps || replay_mem) && (!rollout_path || resume))
        return fprintf(stderr, "chain: --stage-log, --crossings and --replay-steps need --rollout-out, not --resume\n"), 2;
    if (cross_dir && (!stage_path || n != 1))
        return fprintf(stderr, "chain: --crossings needs --stage-log and --n 1\n"), 2;
    if (!!replay_steps != !!replay_mem || (replay_steps && (replay_script || replay_leg || n != 1)))
        return fprintf(stderr, "chain: --replay-steps DIR:N needs --replay-mem FILE, --n 1, not --replay-script\n"), 2;
    if (stage_path)
    {
        int fd = open(stage_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(stage_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --stage-log %s: %s\n", stage_path, strerror(errno)), 1;
        fprintf(stage_out, "attempt\tevent\tstep\tworld_time\tdim\tx\ty\tz\thealth\tfood\tfood_points\tkind\tmbit\tmem\n");
    }
    if (wood_path)
    {
        if (!rollout_path || resume) return fprintf(stderr, "chain: --wood-log needs --rollout-out, not --resume\n"), 2;
        int fd = open(wood_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(wood_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --wood-log %s: %s\n", wood_path, strerror(errno)), 1;
        fprintf(wood_out, "# S attempt x0 y0 z0 near17 near162 n24_17 n24_162 clear24_17 clear24_162 clear_near loaded "
                          "(PRIVILEGED: the client world's blocks at the first step)\n"
                          "# R attempt step x y z kind mbit eye_water air free seen17_n seen17_d mined_logs logs_held\n"
                          "# E attempt result step first_seen17 seen17_d first_aim first_mined first_held first_wpick "
                          "maxd ymin ymax water_steps full_steps roofed10 free_end mined_logs own_by_kind(none,surface,"
                          "screen,loot,mine,craft) surface_label_by_branch(air_rescue,provision,swim,feet_water,dry,"
                          "other) rescue_runs rescue_over_1000 rescue_longest wood_search(seeks,sweeps,turns,steps)\n");
    }
    if (cross_dir)
    {
        char cd[2200];
        snprintf(cd, sizeof cd, "%s/seg", cross_dir);
        if ((mkdir(cross_dir, 0777) && errno != EEXIST) || (mkdir(cd, 0777) && errno != EEXIST))
            return fprintf(stderr, "chain: --crossings %s: %s\n", cross_dir, strerror(errno)), 1;
    }
    if (replay_steps)
    {
        char sf[2200];
        snprintf(sf, sizeof sf, "%s", replay_steps);
        char *colon = strrchr(sf, ':');
        if (!colon) return fprintf(stderr, "chain: --replay-steps DIR:N\n"), 2;
        *colon = 0;
        int64_t want = atoll(colon + 1);
        struct segf s;
        if (seg_read(sf, 1, &s)) return 1;
        if (want < 1 || want > s.h.n)
            return fprintf(stderr, "chain: --replay-steps %s: %lld of %lld steps\n", sf, (long long)want, (long long)s.h.n), 2;
        leg_acts = calloc((size_t)want, sizeof *leg_acts);
        for (int64_t t = 0; t < want; ++t) leg_acts[t] = s.s[t].act;
        leg_n = want;
        free(s.s);
        printf("replay-steps: %lld acts of %s's %lld, then %s\n", (long long)leg_n, sf, (long long)s.h.n, replay_mem);
    }
    /* lane/choice */
    if (replay_reseed && !replay_mem) return fprintf(stderr, "chain: --replay-reseed S needs --replay-mem\n"), 2;
    if ((choice_path || mem_at) && !rollout_path)
        return fprintf(stderr, "chain: --choice-log and --mem-at need --rollout-out\n"), 2;
    if (mem_at)
    {
        const char *colon = strchr(mem_at, ':');
        if (!colon || n != 1) return fprintf(stderr, "chain: --mem-at N:FILE, with --n 1\n"), 2;
        mem_at_n = atoll(mem_at);
        snprintf(mem_at_file, sizeof mem_at_file, "%s", colon + 1);
        if (mem_at_n < 1) return fprintf(stderr, "chain: --mem-at N:FILE: N from 1\n"), 2;
    }
    if (choice_path)
    {
        int fd = open(choice_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(choice_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --choice-log %s: %s\n", choice_path, strerror(errno)), 1;
        fprintf(choice_out, "row\tattempt\tresult\tstep\tenv_step\tqueries\tconflicts\tarmor\tipick\tinvalid\tcheck\t"
                            "check_diff\tplan\tingots\tsticks\tdmg\tchose\twhy\tiron_picks\tdia_picks\tingots_now\t"
                            "worn\tdim\thealth\tfood\n");
    }
    snprintf(S.dir, sizeof S.dir, "%s", dir);
    char sd[2200];
    snprintf(sd, sizeof sd, "%s/seg", dir);
    if (mkdirs(sd)) return fprintf(stderr, "chain: %s: %s\n", sd, strerror(errno)), 1;
    char tp[2200];
    snprintf(tp, sizeof tp, "%s/nodes.tsv", dir);
    if (!(S.ftsv = fopen(tp, "w"))) return fprintf(stderr, "chain: %s: %s\n", tp, strerror(errno)), 1;
    FILE *fseg = NULL;
    if (segment_log)
    {
        fseg = fopen(segment_log, "w");
        if (!fseg) return fprintf(stderr, "chain: %s: %s\n", segment_log, strerror(errno)), 1;
        fprintf(fseg, "env\tparent\tsteps\tworld_time\thealth\tfood\tfood_points\tpearls\tprovoked\tkilled\t"
                      "hit_presses\tdeaths\tfirst_cause\tlock_killed\tlock_dead\tlock_day\tlock_night\tlock_other\t"
                      "seen_new\tprovoked_new\tkilled_new\titem_steps_new\titem_walk_new\thome_steps_new\t"
                      "world_day\tday_tick\tstart_world_time\n");
    }
    FILE *rollout_out = NULL;
    if (rollout_path)
    {
        int fd = open(rollout_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0)
            return fprintf(stderr, "chain: REFUSE --rollout-out %s: %s\n", rollout_path, strerror(errno)), 1;
        rollout_out = fdopen(fd, "w");
        if (!rollout_out)
        {
            int why = errno;
            close(fd);
            return fprintf(stderr, "chain: --rollout-out %s: %s\n", rollout_path, strerror(why)), 1;
        }
        fprintf(rollout_out, "attempt\tseed\tfrom_node\tfrom_rung\ttarget\tresult\tticks\tlast_rung\tlast_owner_bit\tlast_kind\thealth\tfood\tegg\tfire\tair\tfood0\tfall\n");
    }
    FILE *trace_out = trace_path ? fopen(trace_path, "w") : NULL;
    if (trace_path && !trace_out) return fprintf(stderr, "chain: %s: %s\n", trace_path, strerror(errno)), 1;
    FILE *death_out = NULL;
    if (death_log_path)
    {
        /* A second search must not truncate the first search's evidence. O_EXCL claims a new path atomically,
         * including when a different process has already opened it for writing. */
        int fd = open(death_log_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0)
            return fprintf(stderr, "chain: REFUSE --death-log %s: %s\n", death_log_path, strerror(errno)), 1;
        death_out = fdopen(fd, "w");
        if (!death_out)
        {
            int why = errno;
            close(fd);
            unlink(death_log_path);
            return fprintf(stderr, "chain: --death-log %s: %s\n", death_log_path, strerror(why)), 1;
        }
    }
    if (end_log_path)
    {
        /* --end-log FILE (lane/legs, with --rollout-out; off by default): a row an attempt as it ends, the End books
         * (neth_vec_dragon_info): the dimension and place at the end, the dragon's health, the macro's phase, and the
         * End's first death: its step, cause (vecdragon.h DK_*), wing push, phase (DP_*), place and fall */
        if (!rollout_path) return fprintf(stderr, "chain: --end-log needs --rollout-out\n"), 2;
        int fd = open(end_log_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(end_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --end-log %s: %s\n", end_log_path, strerror(errno)), 1;
        fprintf(end_out, "attempt\tresult\tticks\tdim\tx\ty\tz\thealth\tdragon_health\tphase\tdeaths\tdeath_step\tcause\t"
                         "pushed\td_phase\td_x\td_y\td_z\td_fall\tpushes\tenders\n");
    }
    if (etrace_arg)
    {
        /* --end-trace I:FILE (lane/legs, with --rollout-out; off by default): attempt I's (I -1: every attempt's) every
         * step in the End: place, look, health, the dragon's health in view, the dragon macro's phase (DP_*), the wing
         * pushes so far, the kind, flags, the attempt */
        const char *p = strchr(etrace_arg, ':');
        if (!rollout_path || !p) return fprintf(stderr, "chain: --end-trace I:FILE needs --rollout-out\n"), 2;
        etrace_i = atoi(etrace_arg);
        int fd = open(p + 1, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(etrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --end-trace %s: %s\n", p + 1, strerror(errno)), 1;
        fprintf(etrace_out, "step\tx\ty\tz\tyaw\tpitch\thealth\tdragon_health\tphase\tpushes\tkind\tflags\tattempt\tunder\tahead%s%s\n",
                etrace_hunt ? "\tsub\thunt\thunt_x\thunt_y\thunt_z\thunt_hunts\thunt_shots\ttarget\taim_x\taim_y\taim_z\t"
                              "tries\tlob\thtower\tkept\thunt_in_view\tin_view\tshots\tcrystals_hit\tsnowballs\thotbar\tblocks" : "",
                etrace_ender ? "\tender_alive\tender_n\tender_d\tender_scream\tscream_d\tender_cone\tcone_d\tsv_dragon_ticks\tsv_dragon_hp"
                                       "\tsv_dragon_x\tsv_dragon_y\tsv_dragon_z\tvoid_d\tswings\tswing_dmg\tswings_took"
                                       "\tarrows_at\tarrow_dmg\tbites\theal\troof\tsv_ncry\tsv_heal\theal_x\theal_y\theal_z" : "");
    }
    if (death_out) fprintf(death_out, "env\tparent\trung\tstep\tworld_time\tdim\tx\ty\tz\tegg\tfire\tair\tfood0\tfall\tmacro\n");
    if (target_gain && (!rollout_path || target_stage >= 0 || target_gain < 1 || target_gain > CH_GAIN_MAX))
        return fprintf(stderr, "chain: --target-gain [KIND:]N (N 1..%d) needs --rollout-out and no --target-stage\n",
                       CH_GAIN_MAX), 2;
    if (gain_path)
    {
        if (!target_gain) return fprintf(stderr, "chain: --gain-log FILE needs --target-gain\n"), 2;
        int fd = open(gain_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(gain_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --gain-log %s: %s\n", gain_path, strerror(errno)), 1;
        fprintf(gain_out, "attempt\tresult\tsteps\tcount0\tcount_max\tcount_end\tgains\thealth\tfood\tfood_pts\tkind\t"
                          "owner_bit\tx\ty\tz\tnt_phase\tnt_mode\tkills\tbkills\tbstat\tcamp_age\tcobble\tdeath_cause\n");
    }
    if (census_path)
    {
        if (!rollout_path) return fprintf(stderr, "chain: --owner-census FILE needs --rollout-out\n"), 2;
        int fd = open(census_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(census_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --owner-census %s: %s\n", census_path, strerror(errno)), 1;
        fprintf(census_out, "attempt\tresult\tticks\towner_steps\tkind_steps\thand_steps\n");
    }
    if (hunt_path)
    {
        /* --hunt-log FILE (lane/proom, with --rollout-out; off by default): a row an attempt as it ends: the pearls and
         * eyes held at its first step, at most and now; each new count's step, day time (world time mod 24000) and
         * owning macro bit; at the end the day time, the owner, the kind, health, food, food points held, armour
         * points, the feet's y, the sky over the eye (neth_vec_sh_trace's sky: the server's blocks, a label), livings in
         * view; the pearl books (neth_vec_pearl, the episode so far): the death's cause, macro bit, kind, pearl phase,
         * y and steps in water, the hunt's steps by what held it (why, pearl.c PG_WHY_*) and by phase (PP_*) */
        if (!rollout_path) return fprintf(stderr, "chain: --hunt-log FILE needs --rollout-out\n"), 2;
        int fd = open(hunt_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(hunt_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --hunt-log %s: %s\n", hunt_path, strerror(errno)), 1;
        fprintf(hunt_out, "attempt\tresult\tticks\tn0\tn_max\tn_end\tgains\twt\towner\tkind\thealth\tfood\tfood_pts\tarmor\t"
                          "y\tsky\tnvis\tcause\td_mbit\td_kind\td_phase\td_y\td_wet\tseen\tprovoked\tkilled\twhy\tphase_steps\t"
                          "hits\tdrops\tfetched\tfetch_out\titem_steps\tprov_end\tnight_hunt\tender_view\t"
                          "night_steps\talive64\talive128\tender_kills\tpearl_items_new\treach_blind\tblind_swings\t"
                          "reach_swings\tpatrol(steps,open,up,dug,drop,turned,skydig,roofed)\n");
    }
    if (dtrace_path)
    {
        /* --death-trace FILE (lane/proom, with --rollout-out; off by default, read only): a dead attempt's last DT_RING
         * steps, oldest first, written at its death: the steps before it, the day time, the owning macro bit and kind,
         * the pearl macro on and its phase (PP_*), place, the feet's height over the ground and the open sky, health,
         * food, food points, armour points, the held item, attack pressed, fire, on the ground; the pearl target's
         * scream, its distance and the endermen in view (the macro's); the census's (PRIVILEGED) hostiles within 16 by
         * kind and the skeletons with a clear line, and the nearest skeleton, zombie, spider, creeper, enderman */
        if (!rollout_path) return fprintf(stderr, "chain: --death-trace FILE needs --rollout-out\n"), 2;
        int fd = open(dtrace_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(dtrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --death-trace %s: %s\n", dtrace_path, strerror(errno)), 1;
        fprintf(dtrace_out, DT_HEADER);
    }
    if (ktrace_path)
    {
        /* --kill-trace FILE (lane/pground, read only): at each step the stats' endermen killed rise, the attempt's last
         * DT_RING steps in --death-trace's columns (the ring is kept for it with or without --death-trace) */
        int fd = open(ktrace_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(ktrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --kill-trace %s: %s\n", ktrace_path, strerror(errno)), 1;
        fprintf(ktrace_out, DT_HEADER);
    }
    if (lvlog_path)
    {
        /* --lava-log FILE (lane/lava, with --rollout-out; off by default, books only): a dead attempt's last LV_RING
         * steps, oldest first, written at its death (any cause): the owner (label bit, kind, the dispatch's branch),
         * place, look, the act (held keys hex, attack and use pressed, the hotbar), fire, health, on the ground, the eye
         * in water, the held item, the crosshair (its block and offset from the feet's cell), the liquid ray; the
         * macros' own lava senses at the step's end (neth_vec_lava: lava in view within 8, lava_edge's test, strip2's
         * ahead and near tests, the obsidian search's, the escape running, the last lava hit's age); PRIVILEGED the
         * client world's cells round the body (lv_row's comment) */
        if (!rollout_path) return fprintf(stderr, "chain: --lava-log FILE needs --rollout-out\n"), 2;
        int fd = open(lvlog_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(lvlog_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --lava-log %s: %s\n", lvlog_path, strerror(errno)), 1;
        fprintf(lvlog_out, "attempt\tbefore\tstep\tmbit\tkind\tlsrc\tx\ty\tz\tyaw\tpitch\thold\tp_att\tp_use\thotbar\t"
                           "fire\thealth\tog\teye_water\theld\tmo_hit\tmo_dx\tmo_dy\tmo_dz\tmo_block\tlq_hit\tlq_block\t"
                           "lq_d\thot_n\thot_d\tedge\tst_hot\tst_near\tob_hot\tesc\tsdir\thit_age\tb_feet\tb_head\t"
                           "b_under\tlv_touch\tlv_1\tlv_box\tlv_dx\tlv_dy\tlv_dz\tmo_lavanb\tfire_t\twater_d\n");
    }
    if (nlog_path)
    {
        /* --night-log FILE (lane/pnight, with --rollout-out; off by default, read only): a row an attempt a night, written
         * at each dawn it lives to (result dawn) and at its end (death, limit or reached: the night it ended in, partial):
         * the night (1 the first), the step, the day time, health, food, food points, armour points, pearls and eyes
         * held, the night's gain, its lowest health, the stats' endermen killed in it, the distinct endermen seen in view
         * and seen screaming, the steps with one in view, the night's living steps (13000 to 23000) and the census's
         * endermen within 64 and 128 summed over them (PRIVILEGED, counting only), the pearl macro's steps by phase
         * (PP_* order) and the night's steps by owning macro bit (bit:steps, 32 none) */
        if (!rollout_path) return fprintf(stderr, "chain: --night-log FILE needs --rollout-out\n"), 2;
        int fd = open(nlog_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(nlog_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --night-log %s: %s\n", nlog_path, strerror(errno)), 1;
        fprintf(nlog_out, "attempt\tnight\tresult\tstep\twt\thealth\tfood\tfood_pts\tarmor\theld\tgain\thmin\tekills\tseen\t"
                          "screamed\tview_steps\tnight_steps\talive64\talive128\tphase_steps\towners\tfood_gain\t"
                          "food_spent\tfood_seen\tmob_kills\twhy\twhom\tgate\tfirst_seen_phase\tfirst_seen_dist\t"
                          "oracle\tnear64\tfirst_seen_at\ttower\n");
    }
    if (flog_path)
    {
        /* --floor-log FILE --floor-box BOX (lane/pground; off by default, read only, PRIVILEGED): struct fl_acc's rows */
        if (!fl_box_set) return fprintf(stderr, "chain: --floor-log FILE needs --floor-box\n"), 2;
        int fd = open(flog_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(flog_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --floor-log %s: %s\n", flog_path, strerror(errno)), 1;
        fprintf(flog_out, "# box %d,%d,%d,%d,%d,%d\n# F attempt period half why steps held0 held1 ekills ender_born "
                          "ender_in other_born other_in mon_steps ender_steps samples capn_sum cap_sum contacts4 "
                          "provoked home_steps sky_max blk_max air_cells spawnable\n"
                          "# C attempt period half samples kind(zombie skeleton creeper spider cave_spider enderman "
                          "witch slime silverfish; ticking:frozen sample sums, the overworld's) band(chunks 0-4 5-8 "
                          "9-16 17+ from the player's; ticking:frozen) left_ticking left_frozen came gate_tests gate_open "
                          "gate_walks gate_made (the spawner's monster cap test, ticks reached and passed, the pack "
                          "walks and records made on the open ticks; the period's delta at the samples' grain)\n",
                fl_box[0], fl_box[1], fl_box[2], fl_box[3], fl_box[4], fl_box[5]);
    }
    if (dlog_path)
    {
        /* --dig-log FILE (lane/pdig, with --rollout-out; off by default, read only, PRIVILEGED): struct dg_acc's rows */
        if (!rollout_path) return fprintf(stderr, "chain: --dig-log FILE needs --rollout-out\n"), 2;
        int fd = open(dlog_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(dlog_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --dig-log %s: %s\n", dlog_path, strerror(errno)), 1;
        fprintf(dlog_out, "# V attempt id why wt0 b48(first,last,day,night) b32 b16 dmin dy_at_dmin e32(dx,dy,dz,sky) "
                          "c32(air,stone,loose,water,lava,hard) e48 c48\n"
                          "# A attempt result steps steps(day,night) es48(d,n) es32 es16 dy32(<-16,-16..-6,-6..6,>6 as d,n) "
                          "line32(lava,water,loose as d,n) strip(cells,steps,cobble) pearl(home_digs,oracle_steps,oracle_path)\n"
                          "# N attempt night why night_steps sky_steps moving_steps blocks_across sky64 sky128 any64 "
                          "any128\n"
                          "# W attempt night why owners(bit:still:moving, 32 none) phase(walk up scan down drop flee "
                          "guard hide stair tunnel hold travel off; still/moving) pg_why(on dead nether end screen "
                          "noblaze full day yield other policy -) gate(provision nocand cand) oracle(steps d16 d32 d64 "
                          "d64+ path bad ups home; still/all) src(neth_vec_lsrc: 0 macro path, 1 air rescue, 2 shore, "
                          "3 threat; 8 * provision stage (1 surface, 2 stock) + exit wood done screen swim run roam "
                          "idle; k:still:moving) wood(the wood step's branch: 1 screen 2 route chosen 3 dig on the way "
                          "4 walk 5 chop 6 aim at the log, + 8 the craft route; k:still:moving) pv(provision's steps: "
                          "branch.why:still:moving, why provision.c PVW_*: 1 craft wood 2 exit 3 pillar top 4 bridge "
                          "wood 5 stocked 6 stock latch 7 twelve held 8 under with blaze or eyes) idle(provision "
                          "idle by why the strip did not start, macros.c provision_idle_why, + 16 feet at y 62 or "
                          "over; k:still:moving)\n"
                          "# D attempt period why dawn(food,food_pts,raw,cooked) dusk(same) day_steps band.src:steps "
                          "(band 0 food 14+, 1 7-13, 2 under 7) owners(bit:steps) food(eat_starts eats hunts hunt_meat "
                          "meat_got other_got other_eaten scouts scout_found) scout_end(found time below water night "
                          "cut) hunt_end(meat lost time other) trips(n,meat) animals(sum,steps,near<16,<32,<64,64+) "
                          "pvw(k.hi.sky:steps) scout_ends_by_why(n:far_sum:with_animal:near_sum:dy_sum) "
                          "food_owned(still,moving)\n");
    }
    if (slog_path)
    {
        /* --sight-log FILE (lane/pdig, with --rollout-out; off by default, read only): struct sg_acc's rows */
        if (!rollout_path) return fprintf(stderr, "chain: --sight-log FILE needs --rollout-out\n"), 2;
        int fd = open(slog_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(slog_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --sight-log %s: %s\n", slog_path, strerror(errno)), 1;
        fprintf(slog_out, "attempt\tid\tfirst\twt0\td0\tphase\towner\tview_steps\tlast_view\tscream\tdmin\twhy\td_end\t"
                          "sky_end\tdawn\thits\thp_scream\thp_min\thurt\tphase_scream\tsky_scream\tteleports\ttp_last\t"
                          "hit_last\tmoved_after_view\towner_end\tphase_end\n");
    }
    if (ntrace_arg)
    {
        const char *p = strchr(ntrace_arg, ':');
        if (!rollout_path || !p) return fprintf(stderr, "chain: --nether-trace I:FILE needs --rollout-out\n"), 2;
        ntrace_i = atoi(ntrace_arg);
        int fd = open(p + 1, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(ntrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --nether-trace %s: %s\n", p + 1, strerror(errno)), 1;
        /* lane/rods: FILE.rods, a line each time a blaze rod item appears or vanishes (books only) */
        char rp[4200];
        snprintf(rp, sizeof rp, "%s.rods", p + 1);
        int rfd = open(rp, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (rfd < 0 || !(rodlog_out = fdopen(rfd, "w")))
            return fprintf(stderr, "chain: REFUSE --nether-trace %s: %s\n", rp, strerror(errno)), 1;
        fprintf(rodlog_out, "step\tattempt\tev\titem\tage\tcount\tx\ty\tz\tdx\tdy\tdz\tcell\tunder\trods\thood\thealth\n");
        fprintf(ntrace_out, "step\tattempt\tx\ty\tz\tyaw\tpitch\thealth\towner_bit\tkind\tnt_phase\tnt_mode\tfort\tspawner\t"
                            "goal\tcobble\tunder\tfeet\thead\tahead_under\tahead_feet\tflags\t"
                            "dig_step\tdig_cell\tdig_block\tdig_lava\tdig_seen\trefills\t"
                            "camp_age\tblz16\tblzsp\tsp_block\thead1\thead2\tkills\trods\tblz_los\tblz_up\tblz_dy10\t"
                            "blz_hd10\tfood\tfood_pts\tfid\tfkind\tdoing\thood\trc\thold\tpress\tbseen_n\tbseen_d\t"
                            "fstuck\tdrive_t\tpick_x\tpick_y\tpick_z\tpick_age\trods_ground\trod_d\trod_dy\thood_open\thood_in\thood_k\tbhits\tmo_ent\tmo_ekind\tmo_blk\tlk_d\tlk_dy\tlk_los\t"
                            "dealt\ttaken\tcm_dt\tcm_name\tcm_dmg\tcm_age\tsfire\tfb_blz\tfb_los\tfb_n\tfb_new\tfb_blk\tlos_d\tlos_dy\tcov_here\tcov_off\tlos_cone\teye_strikes\tlos_mouth\tlos_cell\tmouth_steps\tmouths\tarm_dmg\tfire_near\thood_fire\n");
    }
    if (shtrace_arg)
    {
        const char *p = strchr(shtrace_arg, ':'), *k = strchr(shtrace_arg, '/');
        if (!rollout_path || !p) return fprintf(stderr, "chain: --shold-trace I[/K]:FILE needs --rollout-out\n"), 2;
        shtrace_i = atoi(shtrace_arg);
        if (k && k < p) shtrace_k = atoi(k + 1);
        if (shtrace_k < 1) return fprintf(stderr, "chain: --shold-trace K must be 1 or more\n"), 2;
        int fd = open(p + 1, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(shtrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --shold-trace %s: %s\n", p + 1, strerror(errno)), 1;
        fprintf(shtrace_out, "step\tattempt\tx\ty\tz\tyaw\tpitch\thealth\tfood\towner_bit\tkind\tscreen\twtime\teyes\t"
                             "snowballs\tgo\tphase\tosub\ton\twant\tfwait\tsn_hold\tsn_w0\tsn_quit\tfood_pts\tfood_wait\t"
                             "armor\tarmor_wait\tswords\tsword_need\tsword_wait\troom_ok\troom_d\troom\tlit\tfilled\t"
                             "gave_up\tbook_phase\tflags\tstand\tnop\topk\tofail\tooff\tdig\tspawner\tbox\tprov\tfs_home\tsky\tpearl_on\tpearl_phase\tpearls\tnvis\tnframe\tframe_d\trstart\twood\twood_blk\twood_stand\twood_logs\twood_age\tact_hold\tact_look\t"
                             "act_attack\tmo_entity\tmo_ekind\tlq_hit\tlq_block\tptid\tpangry\tptdist\tphits\n");
    }
    if (ladder_path)
    {
        /* --ladder-log FILE (lane/dleg, with --rollout-out; books only, off by default): a row an attempt as it ends: the
         * first step each End rung held (stage_hit's reading with --target-stage's BASE; crystals_gone10 every crystal,
         * exit the overworld after the End; -1 never), the books at its first End step (snowballs, arrows, a bow, the best
         * sword's id, armor points, food points held, health, the hunger bar, place) and at its end, the lowest health
         * and dragon health seen */
        if (!rollout_path) return fprintf(stderr, "chain: --ladder-log FILE needs --rollout-out\n"), 2;
        int fd = open(ladder_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(ladder_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --ladder-log %s: %s\n", ladder_path, strerror(errno)), 1;
        for (int k = 0; k < LAD_RUNGS; ++k) ld_stage[k] = k == 4 || k == 9 ? -1 : stage_by_name(LAD_NAME[k]);
        fprintf(ladder_out, "attempt\tresult\tsteps");
        for (int k = 0; k < LAD_RUNGS; ++k)
            if (!strncmp(LAD_NAME[k], "crystals_gone", 13)) fprintf(ladder_out, "\thits%s", LAD_NAME[k] + 13);
            else fprintf(ladder_out, "\t%s", LAD_NAME[k]);
        fprintf(ladder_out, "\tsnow0\tarrows0\tbow0\tsword0\tarmor0\tfpts0\thealth0\tfood0\tx0\ty0\tz0"
                            "\tsnow_end\tarrows_end\thealth_end\tfpts_end\thealth_min\tdragon_min\tx\ty\tz"
                            "\tcry_seen\tcry_gone\tcry_last\tcry_gone75\tcry_standing\n");
    }
    if (build_path)
    {
        if (!rollout_path) return fprintf(stderr, "chain: --build-log FILE needs --rollout-out\n"), 2;
        int fd = open(build_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(build_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --build-log %s: %s\n", build_path, strerror(errno)), 1;
        fprintf(build_out, "attempt\tresult\tsteps\tobs0\tobs_max\tobs_end\tkeep\tfirst_obs\tobs10\tobs_keep\tplan\t"
                           "frame\tlit\tnether\tbp_starts\tbp_steps\tbp_sites\tbp_casts\tbp_best\tbp_lit\tob_mined\t"
                           "dim\tx\ty\tz\tobs_who_steps\tfp0\tfp_keep\tfp_frame\tfp_nether\thunger_nether\t"
                           "food_all\tfood_wait\n");
        /* food_all, food_wait (the food census over the attempt, and from obs_keep on; '-' never kept), comma lists:
         * hunts, hunt ends (meat got, lost, out of time, other), lost hunts by why (killed with the drop not picked up,
         * hit not killed, never hit with its last place reached, not reached, in water), scouts, scouts that found,
         * scout ends (found, out of time, below the surface, water, night/held off/screen, cut short), meat got, meat
         * got hunting, other food got, eatings done, trips, trip meat, steps with a food animal in view */
    }
    if (supply_path)
    {
        if (!rollout_path) return fprintf(stderr, "chain: --supply-log FILE needs --rollout-out\n"), 2;
        int fd = open(supply_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(supply_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --supply-log %s: %s\n", supply_path, strerror(errno)), 1;
        fprintf(supply_out, "attempt\tresult\trow\tstep\tdim\thealth\tfood\tfpts\tcooked\tarmor\tboots\tlegs\tchest\t"
                            "helm\tsword\tblocks\tbucket\twater\tlava\tingot\tore\tobs\tores_dug\tt_ore1\tt_worn4\tarm_steps\tpicks\tgu_noway\tgu_walked"
                            "\tr_turn\tr_pick\tr_dug\tr_iron24\tr_furnace\tr_ingot24\tr_has_boots\tr_has_legs\tr_has_chest\tr_has_helm"
                            "\tr_worn_boots\tr_worn_legs\tr_worn_chest\tr_worn_helm\tfree\tpicks_held\n");
    }
    if (ptrace_arg)
    {
        const char *p = strchr(ptrace_arg, ':'), *k = strchr(ptrace_arg, '/');
        if (!rollout_path || !p) return fprintf(stderr, "chain: --portal-trace I[/K]:FILE needs --rollout-out\n"), 2;
        ptrace_i = atoi(ptrace_arg);
        if (k && k < p) ptrace_k = atoi(k + 1);
        if (ptrace_k < 1) return fprintf(stderr, "chain: --portal-trace K must be 1 or more\n"), 2;
        int fd = open(p + 1, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 || !(ptrace_out = fdopen(fd, "w")))
            return fprintf(stderr, "chain: REFUSE --portal-trace %s: %s\n", p + 1, strerror(errno)), 1;
        fprintf(ptrace_out, "step\tattempt\tx\ty\tz\tyaw\tpitch\thealth\tfood\towner_bit\tkind\twtime\tobs\tkeep\tbucket\t"
                            "water\tlava\tflint\tdpick\tcobble\tdirt\tfurnace\tbp_want\tbp_start\tbp_next\tbp_on\t"
                            "bp_mode\tbp_site\tbp_step\tbp_mem_w\tbp_mem_l\tbp_pick_w\tbp_pick_l\tbp_off\tob_want\tob_on\t"
                            "ob_job\tob_who\tob_search\tob_mem_w\tob_mem_l\tob_off\tpp_want\tpp_step\tpp_retry\t"
                            "frame_cells\tportals\tsky\tmo_block\tfood_pts\tprov\tfs_on\tfs_n\tfs_off\thunt\tfood_n\t"
                            "food_d\tfood_sky\tpp_on\tob_why\tob_why_step\thunt_vis\tmeat\thunt_cap\thunt_off\tpv\t"
                            "fo_have\tfo_n\thunt_d\tst3\tst_night\tst_yield\tst_on\tsoff\thk_vis\tflost\thpk\t"
                            "hkilled\thage\tgstuck\testeps\tsw\thkind\traw\tcoal\tplanks\tcplan_next\tscreen\tfin\tfin_n\tffuel\tffuel_n\tfout\tfout_n\t"
                            "cplan\tcphase\ttables\thas_table\tnear_tb\tnear_fu\tpl_sky\ton_ground\teye_water\tobm_n\tobm_bad\tobm_up\tobm_adj\tobm_far\t"
                            "obm_ok\tobm_dy\tob_scs\tobm_d\tobm_src\tobm_flow\tobm_gone\tobm_srcd\t"
                            "picks\tpk_left\tingot\tstick\tarm_pts\n");
    }
    if (trace_out) fprintf(trace_out, "step\tworld_time\tx\ty\tz\troom_distance\thealth\tfood_bar\theld_food_points\teyes_held\tmacro_kind\tsh_phase\tsh_oracle_subphase\troom_step\tframe_step\tfilled\tbridges\tthrows\tnear\tlost\thides\thide_steps\tskies\tpaths\tstuck\tprogress\tscouts\tscout_found\thunts\tmeat_got\tanimals_128\tanimal_x\tanimal_z\traw_meat\tcooked_meat\tair\tfree_slots\tpearl_phase\tpearl_on\titems_32\titem_x\titem_y\titem_z\titem_walk\titem_full_steps\titem_wet_steps\titem_walk_fail\titem_walk_moving\titem_walk_there\titem_walk_timeover\tlogs\tplanks\tcoal\tfurnace\tmacro_bit\tsh_wait_elapsed\tportal_step\tportal_x\tportal_y\tportal_z\tscout_outtime\tscout_below\tscout_night\tscout_cut\tact_hold\tact_press_attack\tact_look_mode\tact_hotbar\tact_yaw\tact_pitch\tmo_hit\tmo_block\tmo_x\tmo_y\tmo_z\tflags\tarmor_on\tarmor_ore\tarmor_ingot\tarmor_points\tarmor_plans\tnether_phase\tnether_fort_has\tnether_fort_x\tnether_fort_y\tnether_fort_z\tnether_loc_has\tnether_loc_x\tnether_loc_z\tnether_oracle_paths\tnether_oracle_ok\tnether_oracle_lock\tnether_mode\tnether_cobble\tentry_distance\tfortress_distance\tnether_sp_has\tnether_sp_x\tnether_sp_y\tnether_sp_z\tnether_fortin\n");
    /* (lane/chain2: the images and the save slots from the env count. s42k3 ran 128 envs on --live 96: every image held
     * by an env, each new node the only one free to go whatever its score, and four save slots refused five saves for
     * each one made) */
    if (S.live < n) return fprintf(stderr, "chain: --live %d is under one image an env (%d envs)\n", S.live, n), 1;
    if (S.live < CH_LIVE_PER_ENV * n)
    {
        printf("chain: --live %d raised to %d (%d images an env)\n", S.live, CH_LIVE_PER_ENV * n, CH_LIVE_PER_ENV);
        S.live = CH_LIVE_PER_ENV * n;
    }
    if (save_slots <= 0) save_slots = n / CH_ENVS_PER_SLOT < 4 ? 4 : n / CH_ENVS_PER_SLOT > 32 ? 32 : n / CH_ENVS_PER_SLOT;
    fprintf(S.ftsv, "# chain search: run %s macros %d (%s) options %s; N %d in %d groups, segments %.0f s, free 0..%d, "
                    "keep %d a rung, live %d, camera 20 +- %.0f, keys p %.3f, seed %llu\n",
            run, macros, rf.macro_spec, rf.mopts[0] ? rf.mopts : "-", n, groups, seg_s, S.free_max, S.keep, S.live,
            cam_w, key_p, (unsigned long long)seed);
    fprintf(S.ftsv, "id\tparent\trung\tname\tq\tticks\tseg\twall\thealth\tfood\tdim\tx\ty\tz\tinv\tprog\n");
    S.seg_len = (int64_t)llround(seg_s * 20);

    struct neth_vec_opts o;
    memset(&o, 0, sizeof o);
    o.env.config = run;
    o.env.n = n;
    o.env.threads = threads;
    o.env.device_mesh = -1;
    o.groups = groups;
    o.ep_len = CH_EP_LEN;
    o.macros = macros;
    o.objective = NETH_VEC_FULL;
    o.macro_opts = rf.mopts;
    o.chain = 1;
    o.flags |= NETH_RL_SAVE_SLOTS(save_slots);
    if (dev_start) o.flags |= NETH_RL_DEV, printf("chain: a dev start (--dev 1): a what-if's rows\n");
    printf("chain: %d envs, %d images at most, %d save slots\n", n, S.live, save_slots);
    o.dbench = 1;              /* the books (counting only): the food census at the segments' ends */
    char err[2048];
    double t0 = now_s();
    S.v = neth_vec_make(&o, err, sizeof err);
    if (!S.v) return printf("FAIL: %s\n", err), 1;
    S.h = neth_vec_env(S.v);
    struct neth_vec_bufs b;
    memset(&b, 0, sizeof b);
    neth_vec_buffers(S.v, &b);
    int aw = neth_vec_action_width(S.v);
    printf("chain search: %s, macros %d (%s), options %s, N %d in %d groups, segments of %.0f s; made in %.1f s\n", run,
           macros, rf.macro_spec, rf.mopts[0] ? rf.mopts : "-", n, groups, seg_s, now_s() - t0);
    fflush(stdout);
    S.t0 = now_s();
    for (int r = 0; r < R_N; ++r) S.first_t[r] = -1;

    /* node 0: the run's start */
    struct node *root = node_new(&S);
    root->parent = -1, root->start = 0, root->state = NS_READY, root->rung = R_SPAWN, root->q = 1.5f;
    /* --start-latched (lane/chain3): a library start (lane/killlib's Java-made snapshot at a kill's stage) has no chain
     * behind it, so the latches a proved chain carries (the meat and provisions held before the Nether, the carried
     * obsidian, fed and armed before the End) are taken as passed; else every rung past them reads demoted (an End
     * start read as furnace or obsidian_10, and dragon_75 could never count) */
    if (start_latched) root->prov = root->meat = root->carry = root->fed = root->armed = 1;
    S.first_t[R_SPAWN] = 0;
    int camw = (int)cam_w, keyp = (int)lround(key_p * 1024);

    int rollout_root = rollout_out && !resume ? 0 : -1, rollout_finished = 0;
    struct env *E = calloc((size_t)n, sizeof *E);
    for (int i = 0; i < n; ++i)
    {
        E[i].rng = ((seed + (rollout_out ? (uint64_t)i : 0)) * 0x9e3779b97f4a7c15ull) ^
                   ((uint64_t)(rollout_out ? 1 : i + 1) * 0xbf58476d1ce4e5b9ull);
        E[i].node = 0, E[i].next = -1, E[i].best = R_SPAWN, E[i].gain_at = -1;
        E[i].tg0 = -1;
        E[i].bl_obs0 = -1;
        E[i].bl_first = E[i].bl_10 = E[i].bl_kept = E[i].bl_plan = E[i].bl_frame = E[i].bl_lit = E[i].bl_nether = -1;
        E[i].bl_fp0 = E[i].bl_fpk = E[i].bl_fpf = E[i].bl_fpn = E[i].bl_hn = -1;
        for (int k = 0; k < LAD_RUNGS; ++k) E[i].ld_at[k] = -1;
        E[i].ld_hpmin = E[i].ld_dmin = 1e9f;
        E[i].lc_n = E[i].lc_ngone = 0, E[i].lc_g75 = -1, E[i].lc_last = -1;
        E[i].fed = root->fed, E[i].armed = root->armed;     /* (0 but with --start-latched: the first segment's) */
        E[i].rollout_started = !!(rollout_out && !resume);
        E[i].hunt0 = -1;
        E[i].ek0 = -1;
        E[i].nl_day0 = -1;
        for (int k = 0; k < FE_N; ++k) E[i].fw_at[k] = -1;
        if (rollout_out && !resume && leg_n > 0) neth_vec_chain_play(S.v, i, leg_acts, leg_n);   /* --replay-script */
        if (tape_attempt && rollout_out && !resume && i == atoi(tape_attempt))
        {
            /* --tape-attempt I:PATH (lane/chain3): attempt I written as the oracle's script from the run's start, stitch's
             * form (a Snapshot line, the reset step's no act, then each step's act as the env stepped it, b.acts), to be
             * played on the Java client; closed when the attempt ends */
            const char *p = strchr(tape_attempt, ':');
            if (!p || !(tape_f = fopen(p + 1, "w"))) return fprintf(stderr, "chain: --tape-attempt %s: no file\n", tape_attempt), 2;
            tape_i = i;
            struct neth_rl_act noop;
            struct pool_act pa;
            act_noop(&noop);
            to_pool(&noop, &pa);
            fputs("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n", tape_f);
            moveact_script_line(tape_f, &pa);
        }
        E[i].cap = S.seg_len + 16;
        E[i].log = malloc((size_t)E[i].cap * sizeof *E[i].log);
        E[i].last_te = -1;
        root->held++;
        root->tries++;
        neth_vec_chain_free(S.v, i, (int)(xs(&E[i].rng) % (uint64_t)(S.free_max + 1)));
    }
    if (resume)
    {
        /* the earlier search's best nodes, each played again into an env from the run's start: its chain's acts (the
         * make's step is the first segment's reset step; each later seam's reset step a no act), its steps the
         * segment's log, then saved */
        int ids[1024], rgs[1024];
        int k = resume_pick(resume, resume_k < n ? resume_k : n, ids, rgs, resume_max < 0 ? R_N - 1 : resume_max, S.min_rung,
                            resume_bad, resume_ids);
        if (k < 0) return 1;
        for (int j = 0; j < k; ++j)
        {
            struct segf *c;
            int ns = chain_load(resume, ids[j], &c);
            if (ns < 0) return 1;
            int64_t tot = 0;
            for (int q = 0; q < ns; ++q) tot += c[q].h.n + (q > 0);
            struct env *e = &E[j];
            free(e->log);
            e->cap = tot + S.seg_len + 16;
            e->log = malloc((size_t)e->cap * sizeof *e->log);
            e->racts = malloc((size_t)(tot ? tot : 1) * sizeof *e->racts);
            int64_t t = 0;
            for (int q = 0; q < ns; ++q)
            {
                if (q > 0)
                {
                    memset(&e->log[t], 0, sizeof e->log[t]);
                    e->log[t].act.hotbar = -1;
                    e->log[t].trunc = 1;
                    e->racts[t] = e->log[t].act;
                    ++t;
                }
                for (int64_t u = 0; u < c[q].h.n; ++u, ++t) e->log[t] = c[q].s[u], e->racts[t] = c[q].s[u].act;
            }
            for (int q = 0; q < ns; ++q) free(c[q].s);
            free(c);
            e->replay = tot;
            e->rfrom = ids[j];
            e->rrung = rgs[j];
            neth_vec_chain_play(S.v, j, e->racts, tot);
            neth_vec_chain_free(S.v, j, 0);
            printf("resume: env %d plays %s's node %d again (%d segments, %lld ticks)\n", j, resume, ids[j], ns,
                   (long long)tot + 1);
        }
        fprintf(S.ftsv, "# resumed: the first %d envs play %s's nodes again\n", k, resume);
        S.hold = k > 0 && (hold_on || rollout_out);
    }
    S.segs = n;
    int gs = n / groups;
    int held[256] = {0};
    double tlast = S.t0, sst[8], s0[8];
    neth_vec_stats(S.v, s0);
    int done = 0, rc = 0;
    while (!done)
    {
        int g = neth_vec_recv(S.v, err, sizeof err);
        if (g < 0)
        {
            printf("FAIL: %s\n", err);
            rc = 1;
            break;
        }
        /* --toward locate (lane/chain2; PRIVILEGED, as stronghold_oracle locate): the portal room of the stronghold
         * nearest the first env's feet, read from the world's structure data (shoracle.h), the place toward */
        if (S.toward == 2)
        {
            struct neth_vec_chain_peek pk;
            struct shoracle_room room;
            if (neth_vec_chain_peek(S.v, g * gs, &pk) || shoracle_locate(neth_rl_pipe(S.h), g * gs, pk.x, pk.z, &room) ||
                !room.ok)
            {
                printf("FAIL: --toward locate: no stronghold found\n");
                rc = 1;
                break;
            }
            S.tx = (room.box[0] + room.box[3]) / 2.0 + 0.5, S.tz = (room.box[2] + room.box[5]) / 2.0 + 0.5;
            S.toward = 1;
            printf("toward: the stronghold's portal room at %.1f %.1f (from %.1f %.1f)\n", S.tx, S.tz, pk.x, pk.z);
            fflush(stdout);
        }
        /* the saves being made */
        for (int k = 1; k < S.nn; ++k)
        {
            struct node *nd = &S.nd[k];
            if (nd->state != NS_MAKING) continue;
            int st = neth_rl_start_ready(S.h, nd->start);
            if (st == 0) continue;
            S.nd[nd->parent].pend--;
            if (st == 1)
            {
                nd->state = NS_READY;
                tsv_line(&S, nd);
                for (int r = 0; r <= nd->rung; ++r)
                    if (S.first_t[r] < 0) S.first_t[r] = nd->id, S.first_wall[r] = nd->wall;
                char rn[64];
                rung_name(nd->rung, rn, sizeof rn);
                printf("  node %d: %s (rung %d) from node %d at %.0f s, chain %.1f game min, q %.2f, health %.0f food "
                       "%d dim %d\n",
                       nd->id, rn, nd->rung, nd->parent, nd->wall, nd->ticks / 1200.0, nd->q, nd->p.health, nd->p.food,
                       nd->p.dim);
                fflush(stdout);
            }
            else
            {
                nd->state = NS_FAILED;
                nd->start = -1;
                ++S.saves_failed;
            }
            evict(&S);
        }
        for (int i = g * gs; i < (g + 1) * gs; ++i)
        {
            struct env *e = &E[i];
            int64_t te = b.te[i];
            if (rollout_out && rollout_root > 0 && i > 0 && !e->rollout_started && !e->ending)
            {
                e->next = rollout_root, e->ending = 1;
                S.nd[rollout_root].held++;
                neth_vec_chain_next(S.v, i, S.nd[rollout_root].start, 0);
                neth_vec_chain_end(S.v, i);
            }
            if (b.truncations[i] && e->ending)
            {
                /* reset: its next segment from e->next */
                S.nd[e->node].held--;
                if (e->day) S.day_live--, e->day = 0;
                e->node = e->next, e->next = -1, e->ending = 0;
                int64_t ds = day_steps(&S, &S.nd[e->node]);
                e->len = ds > 0 ? ds : S.seg_len;
                if (S.water_seg > 0 && S.nd[e->node].rung == R_WATER &&
                    inv_n(&S.nd[e->node].p.st, 264) >= 3 && inv_n(&S.nd[e->node].p.st, IT_DIA_PICK) == 0)
                    e->len = S.water_seg;
                if (S.land_seg > 0 && S.nd[e->node].rung == R_LAND) e->len = S.land_seg;
                /* --late-seg S: a segment from eyes_12 on plays S steps (the snow trip, the stronghold, the End) */
                if (S.late_seg > 0 && S.nd[e->node].rung >= R_EYE12) e->len = S.late_seg;
                if (S.snow_seg > 0 && S.nd[e->node].rung == R_SNOW) e->len = S.snow_seg;
                /* --end-seg S (lane/chain2): in the End short segments: a save there replays its segment twice in one of the
                 * library's four save slots */
                if (S.end_seg > 0 && S.nd[e->node].rung >= R_END) e->len = S.end_seg;
                if (ds > 0) e->day = 1, S.day_live++, S.day_segs++;
                e->n = 0, e->gained = 0, e->gain_at = -1, e->cut = 0, e->fed = S.nd[e->node].fed,
                e->armed = S.nd[e->node].armed;
                e->best = S.nd[e->node].rung, e->bprog = S.nd[e->node].prog;
                e->last_te = te;
                if (rollout_out && e->node == rollout_root)
                {
                    e->rollout_started = 1;
                    /* Source replay consumes a different number of policy draws than a held env. Begin every
                     * attempt at its own named seed, independent of replay length and batch position. */
                    e->rng = ((seed + (uint64_t)i) * 0x9e3779b97f4a7c15ull) ^ 0xbf58476d1ce4e5b9ull;
                    if (leg_n > 0) neth_vec_chain_play(S.v, i, leg_acts, leg_n);
                }
                if (neth_vec_pearl(S.v, i, 1, &e->pearl0)) memset(&e->pearl0, 0, sizeof e->pearl0);
                double dragon0[NETH_VEC_DRAGON_INFO] = {0};
                e->crystal0 = neth_vec_dragon_info(S.v, i, dragon0) == 0 ? (int)(dragon0[11] + dragon0[73]) : 0;
                S.nd[e->node].tries++;
                ++S.segs;
                neth_vec_chain_free(S.v, i, (int)(xs(&e->rng) % (uint64_t)(S.free_max + 1)));
            }
            else if (te != e->last_te && e->last_te >= 0 && !e->ending)
            {
                if (e->replay > 0)
                {
                    /* --resume: the found chain played again; at its end the state saved as a node from the root */
                    /* (the save at the first step after it: force_save) */
                    e->last_te = te;
                    if (++e->n == e->replay)
                    {
                        e->replay = 0;
                        char mp[2200], merr[256];
                        snprintf(mp, sizeof mp, "%s/seg/%d.mem", resume, e->rfrom);
                        int mr = neth_vec_chain_memory_read ? neth_vec_chain_memory_read(S.v, i, mp, merr, sizeof merr) : 1;
                        if (mr == 0)
                            printf("resume: env %d node %d macro memory restored\n", i, e->rfrom);
                        else
                            printf("resume: env %d node %d macro memory %s\n", i, e->rfrom,
                                   mr > 0 ? "has no sidecar (old node)" : merr);
                        if (mr != 0 && !fresh_memory)
                        {
                            fprintf(stderr, "FAIL: RESUME_MEMORY env %d node %d: %s; use --fresh-memory 1 only for a deliberate fresh controller\n",
                                    i, e->rfrom, mr > 0 ? "sidecar missing" : merr);
                            rc = 1;
                            break;
                        }
                        if (mr != 0) printf("resume: env %d node %d fresh macro memory forced\n", i, e->rfrom);
                        e->force_save = 1;
                        printf("resume: env %d played %s's node %d again (%lld steps)\n", i, resume, e->rfrom,
                               (long long)e->n);
                    }
                    /* --rollout-out: the root saved on the replay's last step itself, at the source node's own tick
                     * (the search saves a step later, one live act past the node: from that image no recorded leg
                     * replays, and every attempt would start from a sampled step the proved chain never took) */
                    if (!(rollout_out && e->force_save))
                    {
                        env_policy(b.actions + (size_t)i * aw, aw, &e->rng, camw, keyp, keys_off);
                        continue;
                    }
                }
                /* a step: its policy step logged */
                /* (--crossings: an attempt's steps kept, every one) */
                int keep_all = !e->rollout_started || cross_dir;
                if (keep_all && e->n == e->cap)
                {
                    e->cap *= 2;
                    e->log = realloc(e->log, (size_t)e->cap * sizeof *e->log);
                }
                struct rec_step *r = &e->log[keep_all ? e->n : 0];
                ++e->n;
                memset(r, 0, sizeof *r);
                for (int j = 0; j < NETH_VEC_ACTION; ++j) r->action[j] = b.actions[(size_t)i * aw + j];
                r->kind = b.kind[i], r->chosen = b.chosen[i], r->driver = b.driver[i];
                r->reward = b.rewards[i];
                r->act = b.acts[i];
                if (tape_f && i == tape_i && !e->rollout_done)
                {
                    struct pool_act pa;
                    to_pool(&b.acts[i], &pa);
                    moveact_script_line(tape_f, &pa);
                }
                {
                    uint32_t mb = b.label ? b.label[i].mbit : 0;
                    S.drive[S.nd[e->node].rung][mb ? 1 + __builtin_ctz(mb) : 0] += 1;
                }
                for (int j = r->act.nops < 0 ? 0 : r->act.nops; j < NETH_RL_OPS; ++j) memset(&r->act.ops[j], 0, sizeof r->act.ops[j]);
                e->last_te = te;
                struct view w;
                read_view(&S, i, &w);
                w.prov_on = S.prov > 0;
                /* a resumed chain's save: the gates its rung there was past (the Nether's rod states hold neither the
                 * provisions nor the meat any more: e42v, ranked furnace) */
                int rr = e->force_save ? e->rrung : -1;
                w.prov = !S.prov || S.nd[e->node].prov || rr > R_PROV ||
                         (food_points(&w.p.st) >= S.prov && inv_n(&w.p.st, IT_OBSIDIAN) >= 10);
                if (S.prov && !w.prov && w.p.dim == -1) e->ending = 1;   /* the Nether entered unprovisioned: no gain */
                w.meat_on = S.meat > 0;
                w.meat_target = S.meat;
                w.meat = !S.meat || S.nd[e->node].meat || rr > R_MEAT || ((w.p.ms >> 4 & 1u) && meat_n(&w.p.st) >= S.meat);
                w.carry_n = S.carry;
                w.obs_health_min = S.obs_health_min;
                w.obs_food_min = S.obs_food_min;
                w.kit_on = S.kit;
                w.snow_min = S.snow_min;
                w.armor_min = S.armor_min;
                w.post_stock_score = S.post_stock_food > 0 &&
                                     (S.nd[e->node].rung >= R_FOOD_STOCK || e->rrung >= R_FOOD_STOCK);
                w.endfood = w.post_stock_score ? S.post_stock_food : S.endfood;
                w.provision_on = S.provision_on;
                w.late_health_min = S.late_health_min;
                w.late_food_min = S.late_food_min;
                /* A resumed End chain replays its acts without reading the outside-End rows. Its saved End rung came
                 * through those gates in the parent search; food held now may be lower after meals in the End, so the
                 * current inventory cannot reconstruct what it held at the entry. */
                if (e->force_save && e->rrung >= R_END && w.p.dim == 1)
                    e->fed = e->armed = 1;
                /* (--endfood: fed is the food held at the chain's last step outside the End; s42k3f's first End states
                 * walked in with 0 to 21 food points, the gate being the rung's outside the End only) */
                if (w.p.dim != 1) e->fed = w.endfood <= 0 || food_points(&w.p.st) >= w.endfood;
                /* (and a chain in the End unfed ends its segment at once: no step on an End that cannot count) */
                else if (S.end_fed && w.endfood > 0 && !e->fed && !e->force_save && e->replay <= 0) e->ending = 1;
                /* The v2 sidecar restores the dragon book. Add only hits since this segment began; a resumed End node
                 * establishes a fresh baseline rather than counting its inherited hits again at the forced save. */
                w.cry_gone = w.p.dim != 1 ? 0 : e->force_save ? 0 :
                             S.nd[e->node].cry + (int)book_delta(w.crystals, e->crystal0);
                w.toward_on = S.toward;
                w.chamber_stronghold = S.chamber_stronghold;
                w.toward_d = w.p.dim == -1 && S.chamber_stronghold ?
                             hypot(w.p.x - S.tx / 8.0, w.p.z - S.tz / 8.0) :
                             hypot(w.p.x - S.tx, w.p.z - S.tz);
                if (S.room_score_macro && w.p.dim == 0 && inv_n(&w.p.st, IT_EYE) >= 12)
                {
                    double rx, rz;
                    if (neth_vec_chain_room_target(S.v, i, &rx, &rz) == 0)
                        w.toward_d = hypot(w.p.x - rx, w.p.z - rz);
                    else if (S.room_score_prospective &&
                             inv_n(&w.p.st, IT_SNOWBALL) >= (S.snow_min ? S.snow_min : CH_SNOW))
                    {
                        /* lane/chain3: a snow start has no saved stronghold controller target yet. Ask the same
                         * selector its macro will use at start, from this player's feet, instead of scoring a
                         * different room that happened to be nearest the spawn. */
                        struct shoracle_room room = {0};
                        if (shoracle_locate(neth_rl_pipe(S.h), i, w.p.x, w.p.z, &room) == 0 && room.ok)
                        {
                            rx = (room.box[0] + room.box[3]) / 2.0 + 0.5;
                            rz = (room.box[2] + room.box[5]) / 2.0 + 0.5;
                            w.toward_d = hypot(w.p.x - rx, w.p.z - rz);
                        }
                    }
                }
                if (rollout_out && e->rollout_started)
                {
                    /* (the --carry latch as the search sets it below: before it, every attempt past obsidian_20 read
                     * as obsidian_10, so no leg from there on could reach its target) */
                    w.carry = !S.carry || S.nd[e->node].carry || rr > R_OBS20 ||
                              (inv_n(&w.p.st, IT_OBSIDIAN) >= S.carry && inv_n(&w.p.st, IT_FLINT_STEEL) > 0);
                    int rung = rung_of(&w);
                    if (rung > e->best) e->best = rung;
                    if (ladder_out && (w.p.dim == 1 || e->ld_ok))
                    {
                        /* --ladder-log (lane/dleg, books only) */
                        if (!e->ld_ok)
                        {
                            static const int sw[] = {IT_DIA_SWORD, IT_IRON_SWORD, IT_STONE_SWORD, 283, IT_WOOD_SWORD};
                            int best = 0;
                            for (int k = 0; k < 5 && !best; ++k)
                                if (inv_n(&w.p.st, sw[k]) > 0) best = sw[k];
                            float b0[11] = {(float)inv_n(&w.p.st, IT_SNOWBALL), (float)inv_n(&w.p.st, 262),
                                            (float)inv_n(&w.p.st, 261), (float)best, (float)w.armor,
                                            (float)food_points(&w.p.st), w.p.health, (float)w.p.food, (float)w.p.x,
                                            (float)w.p.y, (float)w.p.z};
                            memcpy(e->ld0, b0, sizeof b0);
                            e->ld_ok = 1;
                        }
                        for (int k = 0; k < LAD_RUNGS; ++k)
                        {
                            if (e->ld_at[k] >= 0) continue;
                            int hit = k == 4 ? w.p.dim == 1 && stage_base + w.cry_gone >= 10 :
                                      k == 9 ? w.p.dim == 0 :
                                      ld_stage[k] > 0 && stage_hit(&w, ld_stage[k], stage_base);
                            if (hit) e->ld_at[k] = e->n;
                        }
                        if (w.p.dim == 1)
                        {
                            /* the server's crystals by pillar (books only) */
                            const struct neth_rl_end *lv = neth_vec_end_view(S.v, i);
                            if (lv && lv->on && lv->sv_ncry <= NETH_RL_END_CRYSTALS)
                            {
                                for (int c = 0; c < lv->sv_ncry; ++c)
                                {
                                    int j = 0;
                                    for (; j < e->lc_n; ++j)
                                        if (fabsf(e->lc_p[j][0] - lv->sv_cry[c][0]) < 1.5f &&
                                            fabsf(e->lc_p[j][2] - lv->sv_cry[c][2]) < 1.5f) break;
                                    if (j == e->lc_n && e->lc_n < LAD_CRY)
                                    {
                                        memcpy(e->lc_p[j], lv->sv_cry[c], sizeof e->lc_p[j]);
                                        e->lc_gone[j] = 0, ++e->lc_n;
                                    }
                                    else if (j < e->lc_n && e->lc_gone[j])
                                        e->lc_gone[j] = 0, --e->lc_ngone;   /* in the list again: not gone */
                                }
                                if (e->n % 10 == 0)
                                    for (int j = 0; j < e->lc_n; ++j)
                                    {
                                        if (e->lc_gone[j]) continue;
                                        int held = 0;
                                        for (int c = 0; c < lv->sv_ncry && !held; ++c)
                                            held = fabsf(e->lc_p[j][0] - lv->sv_cry[c][0]) < 1.5f &&
                                                   fabsf(e->lc_p[j][2] - lv->sv_cry[c][2]) < 1.5f;
                                        if (held) continue;
                                        int bx = (int)floorf(e->lc_p[j][0]), by = (int)floorf(e->lc_p[j][1]),
                                            bz = (int)floorf(e->lc_p[j][2]);
                                        if (neth_vec_nether_block(S.v, i, bx, by - 1, bz) < 0) continue;
                                        e->lc_gone[j] = 1, ++e->lc_ngone, e->lc_last = e->n;
                                    }
                            }
                            if (e->lc_g75 < 0 && e->ld_at[5] >= 0) e->lc_g75 = e->lc_ngone;
                        }
                        if (w.p.health < e->ld_hpmin) e->ld_hpmin = w.p.health;
                        if (w.p.dim == 1 && w.dragon_hp >= 0 && w.dragon_hp < e->ld_dmin) e->ld_dmin = (float)w.dragon_hp;
                    }
                    /* lane/fwd: --replay-mem, the restore's macro memory once its N acts are played (the first live
                     * decision is the next one) */
                    if (replay_mem && !e->fw_mem && e->n == leg_n)
                    {
                        char merr[256];
                        if (neth_vec_chain_memory_load(S.v, i, replay_mem, merr, sizeof merr))
                        {
                            fprintf(stderr, "FAIL: --replay-mem: %s\n", merr);
                            rc = 1;
                            break;
                        }
                        e->fw_mem = 1;
                        printf("replay-steps: env %d macro memory %s read at step %lld\n", i, replay_mem, (long long)e->n);
                        if (replay_reseed)
                        {
                            /* lane/choice (--replay-reseed S): the live part's driver draws from sampler S, as an
                             * attempt seeded S begins (attempt 0's rule) */
                            e->rng = ((uint64_t)strtoull(replay_reseed, NULL, 10) * 0x9e3779b97f4a7c15ull) ^
                                     0xbf58476d1ce4e5b9ull;
                            printf("replay-reseed: env %d live draws from sampler %s at step %lld\n", i, replay_reseed,
                                   (long long)e->n);
                        }
                    }
                    /* lane/choice (--mem-at N:FILE): the macro memory at step N (a restore point anywhere, with
                     * --crossings' steps) */
                    if (mem_at && !e->ch_dumped && i == 0 && e->n == mem_at_n)
                    {
                        char merr[256];
                        e->ch_dumped = 1;
                        if (neth_vec_chain_memory_dump(S.v, i, mem_at_file, merr, sizeof merr))
                            printf("mem-at: not written: %s\n", merr);
                        else
                            printf("mem-at: env %d macro memory %s written at step %lld\n", i, mem_at_file, (long long)e->n);
                    }
                    if (choice_out)
                    {
                        /* lane/choice (--choice-log, books only): a row a conflict of the armour craft slot */
                        struct neth_vec_kit kit = {0};
                        neth_vec_kit(S.v, i, &kit);
                        if (kit.ch_conflicts > e->ch_seen)
                        {
                            e->ch_seen = kit.ch_conflicts;
                            fprintf(choice_out, "C\t%d\t-\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%d\t"
                                    "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.2f\t%d\n", i, (long long)e->n,
                                    (long long)kit.ch_last_step, (long long)kit.ch_queries, (long long)kit.ch_conflicts,
                                    (long long)kit.ch_armor, (long long)kit.ch_ipick, (long long)kit.ch_invalid,
                                    (long long)kit.ch_check, (long long)kit.ch_check_diff, kit.ch_last_plan,
                                    kit.ch_last_ingots, kit.ch_last_sticks, kit.ch_last_dmg, kit.ch_last_chose,
                                    kit.ch_last_why, inv_n(&w.p.st, 257), inv_n(&w.p.st, 278), inv_n(&w.p.st, 265),
                                    armor_pts(&w.p.st), w.p.dim, w.p.health, w.p.food);
                            fflush(choice_out);
                        }
                    }
                    /* lane/wood: --wood-log (books only) */
                    if (wood_out && w.p.dim == 0)
                    {
                        if (!e->wl) e->wl = calloc(1, sizeof *e->wl);
                        struct wl_acc *a = e->wl;
                        if (a && !a->init)
                        {
                            a->init = 1, a->x0 = w.p.x, a->y0 = w.p.y, a->z0 = w.p.z, a->ymin = a->ymax = w.p.y;
                            a->seen17 = a->aim = a->mined = a->held = a->wpick = -1, a->seen_d = -1;
                            wl_scan(S.v, i, &w.p, wood_out);
                        }
                        if (a)
                        {
                            const struct neth_rl_state *st = &w.p.st;
                            double dx = w.p.x - a->x0, dz = w.p.z - a->z0, hd = sqrt(dx * dx + dz * dz);
                            if (hd > a->maxd) a->maxd = hd;
                            if (w.p.y < a->ymin) a->ymin = w.p.y;
                            if (w.p.y > a->ymax) a->ymax = w.p.y;
                            if (a->seen17 < 0 && st->seen_n[3] > 0) a->seen17 = e->n, a->seen_d = st->seen[3].d;
                            if (a->aim < 0 && st->mo_hit && (st->mo_block == 17 || st->mo_block == 162)) a->aim = e->n;
                            if (a->mined < 0 && st->mined_logs > 0) a->mined = e->n;
                            if (a->held < 0 && inv_n(st, IT_LOG) + inv_n(st, IT_LOG2) > 0) a->held = e->n;
                            if (a->wpick < 0 && inv_n(st, 270) > 0) a->wpick = e->n;
                            int fr = 0;
                            for (int j = 0; j < 36; ++j) fr += st->inv[j].item <= 0 || st->inv[j].count <= 0;
                            if (st->eye_water) ++a->water;
                            if (!fr) ++a->full;
                            if (w.p.kind >= 0 && w.p.kind < 24) ++a->own[w.p.kind];
                            uint32_t wmb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                            int ls = neth_vec_lsrc(S.v, i), lsl = ls & 0xff;
                            if (lsl == 1)
                            {
                                if (!a->in_rescue) a->in_rescue = 1, a->rescue_t0 = e->n, ++a->rescue_runs;
                                int64_t len = e->n - a->rescue_t0 + 1;
                                if (len > a->rescue_max) a->rescue_max = len;
                                if (len == WL_RESCUE_LONG) ++a->rescue_long;
                            }
                            else a->in_rescue = 0;
                            if (wmb == 1u)
                            {
                                int fb = neth_vec_nether_block(S.v, i, (int)floor(w.p.x), (int)floor(w.p.y + 0.01),
                                                               (int)floor(w.p.z));
                                int k = lsl == 1 ? 0 : lsl >= 8 && lsl < 16 ? 1 : w.p.kind == 1 ? 2 :
                                        lsl == 0 && (fb == 8 || fb == 9) ? 3 : lsl == 0 ? 4 : 5;
                                ++a->surf[k];
                            }
                            if (e->n % 10 == 0)
                            {
                                int bx = (int)floor(w.p.x), bz = (int)floor(w.p.z), roof = 0;
                                for (int y = (int)floor(w.p.y + 1.62) + 1; y < 256 && !roof; ++y)
                                {
                                    int bb = neth_vec_nether_block(S.v, i, bx, y, bz);
                                    roof = bb > 0 && bb != 18 && bb != 161 && bb != 8 && bb != 9 && !wl_clear_block(bb);
                                }
                                a->roofed += roof;
                            }
                            if (e->n % WL_EVERY == 0)
                                fprintf(wood_out, "R\t%d\t%lld\t%.2f\t%.2f\t%.2f\t%d\t%u\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\n", i,
                                        (long long)e->n, w.p.x, w.p.y, w.p.z, w.p.kind,
                                        b.label ? (uint32_t)b.label[i].mbit : 0u, st->eye_water, w.p.air, fr,
                                        st->seen_n[3], st->seen[3].d, st->mined_logs,
                                        inv_n(st, IT_LOG) + inv_n(st, IT_LOG2));
                        }
                    }
                    /* lane/fwd: --stage-log, each event's first hit (books only), and --crossings' memory there */
                    if (stage_out)
                    {
                        int rods = inv_n(&w.p.st, IT_ROD), powder = inv_n(&w.p.st, IT_POWDER);
                        int eyes = inv_n(&w.p.st, IT_EYE), pearls = inv_n(&w.p.st, IT_PEARL);
                        int cry = ladder_out ? e->lc_ngone : 0;
                        for (int k = 0; k < FE_N; ++k)
                        {
                            if (e->fw_at[k] >= 0) continue;
                            int j = k + 1, hit;
                            if (k < FE_STAGES && !strncmp(STAGE_CHAIN[j].name, "crystals_gone", 13))
                                hit = w.p.dim == 1 && ladder_out && cry >= atoi(STAGE_CHAIN[j].name + 13);
                            else if (k < FE_STAGES) hit = stage_hit(&w, j, 0);
                            else if (k == FE_ROD7) hit = rods + (powder + eyes + 1) / 2 >= 7;
                            else if (k == FE_PEARL12) hit = pearls + eyes >= 12;
                            else if (k == FE_EYE12) hit = eyes >= 12;
                            else hit = w.p.dim == 1 && ladder_out && cry >= 10;
                            if (!hit) continue;
                            e->fw_at[k] = e->n;
                            char nm[64], mp[2400] = "-", merr[256];
                            fw_name(k, nm, sizeof nm);
                            if (cross_dir)
                            {
                                snprintf(mp, sizeof mp, "%s/%s.mem", cross_dir, nm);
                                if (neth_vec_chain_memory_dump(S.v, i, mp, merr, sizeof merr))
                                    printf("crossing: %s not written: %s\n", nm, merr), snprintf(mp, sizeof mp, "-");
                            }
                            fprintf(stage_out, "%d\t%s\t%lld\t%lld\t%d\t%.3f\t%.3f\t%.3f\t%.2f\t%d\t%d\t%d\t%u\t%s\n", i, nm,
                                    (long long)e->n, (long long)w.p.world_time, w.p.dim, w.p.x, w.p.y, w.p.z, w.p.health,
                                    w.p.food, food_points(&w.p.st), w.p.kind, b.label ? (uint32_t)b.label[i].mbit : 0u,
                                    mp);
                            fflush(stage_out);
                        }
                    }
                    if (target_gain)
                    {
                        /* --target-gain (lane/rods): the count at the first step, then each new most held */
                        int gn = gain_count(&w.p.st, gain_kind);
                        if (e->tg0 < 0) e->tg0 = e->tg_max = gn;
                        for (; gn > e->tg_max; ++e->tg_max)
                        {
                            int k = e->tg_max - e->tg0;
                            if (k < CH_GAIN_MAX)
                                e->tg_step[k] = e->n, e->tg_hp[k] = w.p.health,
                                e->tg_mb[k] = b.label ? (uint32_t)b.label[i].mbit : 0u;
                        }
                    }
                    if (supply_out)
                    {
                        /* --supply-log (lane/supply, books only): the armour macro's steps, the first Nether step's
                         * holdings */
                        struct neth_vec_kit kit = {0};
                        neth_vec_kit(S.v, i, &kit);
                        if (kit.on && w.p.dim == 0) e->sp_arm += 1;
                        if (!e->sp_rinit)
                        {
                            for (int k = 0; k < SPR_N; ++k) e->sp_rung[k] = -1;
                            e->sp_rinit = 1;
                        }
                        supply_rungs(e->sp_rung, e->n, &w.p.st, &kit, e->sp_arm);
                        if (!(w.p.flags & (PF_DIED | PF_DEAD)) && w.p.health > 0)
                        {
                            if (!e->sp_alive) e->sp_alive = calloc(1, SP_ROW);
                            supply_fmt(e->sp_alive, e->n, &w.p, &kit, e->sp_arm, e->sp_rung);
                            if (!e->sp_entry && w.p.dim == -1)
                            {
                                supply_row(supply_out, i, "-", "entry", e->sp_alive);
                                e->sp_entry = 1;
                            }
                        }
                    }
                    if (build_out || ptrace_out)
                    {
                        /* --build-log, --portal-trace (lane/bportal, books only) */
                        int on = inv_n(&w.p.st, IT_OBSIDIAN);
                        struct neth_vec_bp_trace bq;
                        int bq_ok = neth_vec_bp_trace(S.v, i, &bq) == 0;
                        int fp = bq_ok ? bq.food_pts : -1;
                        if (e->bl_obs0 < 0)
                            e->bl_obs0 = e->bl_obsmax = on, e->bl_keep = bq_ok ? bq.keep : 0, e->bl_fp0 = fp;
                        if (w.p.dim == -1 && e->bl_nether < 0) e->bl_fpn = fp, e->bl_hn = w.p.food;
                        if (on > e->bl_obsmax)
                        {
                            e->bl_obsmax = on;
                            if (e->bl_first < 0) e->bl_first = e->n;
                        }
                        if (on >= 10 && e->bl_10 < 0) e->bl_10 = e->n;
                        if (bq_ok && on >= bq.keep && e->bl_kept < 0)
                        {
                            e->bl_kept = e->n, e->bl_fpk = fp;
                            e->bl_fok = neth_vec_food(S.v, i, &e->bl_fo) == 0;
                        }
                        if (bq_ok && bq.pp_on && w.p.dim == 0 && e->bl_plan < 0) e->bl_plan = e->n;
                        if (w.p.dim == -1 && e->bl_nether < 0) e->bl_nether = e->n;
                        struct neth_vec_bportal bb;
                        int casts = neth_vec_bportal(S.v, i, &bb) == 0 ? bb.casts : 0;
                        int tr = ptrace_out && (ptrace_i < 0 || i == ptrace_i) && w.p.dim == 0 && e->n % ptrace_k == 0;
                        if (w.p.dim == 0 && (tr || (e->n % CH_BL_EVERY == 0 && (e->bl_obsmax >= 10 || casts > 0))))
                        {
                            build_look(S.h, i, w.p.x, w.p.y, w.p.z, &e->bl_cells, &e->bl_portals);
                            if (e->bl_cells >= 10 && e->bl_frame < 0) e->bl_frame = e->n, e->bl_fpf = fp;
                            if (e->bl_portals > 0 && e->bl_lit < 0) e->bl_lit = e->n;
                        }
                        struct neth_vec_sh_trace pq;
                        if (tr && bq_ok)
                        {
                            char ph[16];
                            picks_held(&w.p.st, ph);
                            fprintf(ptrace_out,
                                    "%lld\t%d\t%.3f\t%.3f\t%.3f\t%.1f\t%.1f\t%.1f\t%d\t%u\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t"
                                    "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t"
                                    "%d\t%d\t%lld\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%.1f\t%d\t%d\t%d\t%lld\t"
                                    "%d\t%d\t%d\t%lld\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t%lld\t"
                                    "%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                                    "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%.1f\t%s\t%d\t%d\t%d\t%d\n",
                                    (long long)e->n, i, w.p.x, w.p.y, w.p.z, w.p.yaw, w.p.pitch, w.p.health, w.p.food,
                                    b.label ? (uint32_t)b.label[i].mbit : 0u, w.p.kind, (long long)w.p.world_time,
                                    bq.obs, bq.keep, bq.bucket, bq.water, bq.lava, bq.flint, bq.dpick, bq.cobble,
                                    bq.dirt, bq.furnace, bq.bp_want, bq.bp_start, bq.bp_mode_next, bq.bp_on,
                                    bq.bp_mode, bq.bp_site, bq.bp_step, bq.bp_mem_w, bq.bp_mem_l, bq.bp_pick_w,
                                    bq.bp_pick_l, (long long)bq.bp_off, bq.ob_want, bq.ob_on, bq.ob_job, bq.ob_who,
                                    bq.ob_search, bq.ob_mem_w, bq.ob_mem_l, (long long)bq.ob_off, bq.pp_want,
                                    bq.pp_step, (long long)bq.pp_retry, e->bl_cells, e->bl_portals,
                                    neth_vec_sh_trace(S.v, i, &pq) == 0 ? pq.sky : -1,
                                    w.p.st.mo_hit ? w.p.st.mo_block : -1, bq.food_pts, bq.prov, bq.fs_on, bq.fs_n,
                                    (long long)bq.fs_off, bq.hunt, bq.food_n, bq.food_d, bq.food_sky, bq.pp_on,
                                    bq.ob_why, (long long)bq.ob_why_step, bq.hunt_vis, bq.meat, bq.hunt_cap,
                                    (long long)bq.hunt_off, bq.pv, bq.fo_have, bq.fo_n, bq.hunt_d,
                                    bq.st3, bq.st_night, bq.st_yield, bq.st_on, (long long)bq.soff, bq.hk_vis,
                                    bq.flost, bq.hpk, bq.hkilled, bq.hage, bq.gstuck, (long long)bq.esteps, bq.sw,
                                    bq.hkind, bq.raw, bq.coal, bq.planks, bq.cplan_next, bq.screen, bq.fin, bq.fin_n,
                                    bq.ffuel, bq.ffuel_n, bq.fout, bq.fout_n, bq.cplan, bq.cphase, bq.tables,
                                    bq.has_table, bq.near_tb, bq.near_fu, bq.pl_sky, bq.on_ground, bq.eye_water,
                                    bq.obm_n, bq.obm_bad, bq.obm_up, bq.obm_adj, bq.obm_far, bq.obm_ok, bq.obm_dy,
                                    bq.ob_scs, bq.obm_d, bq.obm_src, bq.obm_flow, bq.obm_gone, bq.obm_srcd,
                                    ph, pick_left(&w.p.st), inv_n(&w.p.st, 265), inv_n(&w.p.st, 280),
                                    armor_pts(&w.p.st));
                        }
                    }
                    if (census_out)
                    {
                        uint32_t mb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                        ++e->own[mb ? __builtin_ctz(mb) : 32];
                        ++e->kinds[w.p.kind >= 0 && w.p.kind < 7 ? w.p.kind : 7];
                        const struct neth_rl_state *hs = &w.p.st;
                        int hb = hs->mo_hit ? hs->mo_block : -1;
                        int rock = hb == 1 || hb == 4 || hb == 14 || hb == 15 || hb == 16 || hb == 21 || hb == 56 ||
                                   hb == 73 || hb == 74 || hb == 87 || hb == 49 || hb == 48 || hb == 129;
                        if (rock && b.label && (b.label[i].keys >> PK_ATTACK & 1))
                        {
                            static const int PKS[5] = {270, 274, 257, 278, 285};
                            int ph = 0, pm = 0;
                            for (int q = 0; q < 36; ++q)
                                for (int k = 0; k < 5; ++k)
                                    if (hs->inv[q].item == PKS[k] && hs->inv[q].count > 0) *(q < 9 ? &ph : &pm) = 1;
                            if (!ph && pm) ++e->hand[mb ? __builtin_ctz(mb) : 32];
                        }
                    }
                    if (target_gain || hunt_out)
                    {
                        int hn = inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE);
                        if (e->hunt0 < 0) e->hunt0 = e->hunt_max = hn;
                        for (; hn > e->hunt_max; ++e->hunt_max)
                        {
                            int k = e->hunt_max - e->hunt0;
                            if (k < 12)
                                e->gain_step[k] = e->n, e->gain_wt[k] = w.p.world_time % 24000,
                                e->gain_mb[k] = b.label ? (uint32_t)b.label[i].mbit : 0u;
                        }
                    }
                    if (dtrace_out || ktrace_out || hunt_out || nlog_out)
                    {
                        /* lane/proom (read only): --death-trace's ring, --hunt-log's night and view counts */
                        struct neth_vec_pearl pn;
                        if (neth_vec_pearl(S.v, i, 1, &pn) != 0) memset(&pn, 0, sizeof pn);
                        int64_t wt = w.p.world_time % 24000;
                        int night = wt >= 13000 && wt < 23000;
                        if (nlog_out)
                        {
                            /* lane/pnight (read only): --night-log's dawns and the night's counts */
                            const struct neth_rl_far *nf = neth_rl_far(S.h, i);
                            int ek = nf ? nf->ender_kills : -1;
                            int held = inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE);
                            int64_t day = (w.p.world_time + 1000) / 24000;
                            int mk = w.p.st.mob_kills, fp = food_points(&w.p.st);
                            if (e->nl_day0 < 0)
                            {
                                if (!e->nl) e->nl = calloc(1, sizeof *e->nl);
                                e->nl_day0 = day, e->nl_k = 1;
                                nl_begin(e->nl, ek, held, w.p.health, mk, fp);
                            }
                            for (; day > e->nl_day0 + e->nl_k - 1; ++e->nl_k)
                            {
                                nl_row(nlog_out, i, e, "dawn", &w, ek, held, mk);
                                nl_begin(e->nl, ek, held, w.p.health, mk, fp);
                            }
                            struct nl_acc *na = e->nl;
                            ++na->steps;
                            {
                                int32_t gb[3] = {0, 0, 0};
                                neth_vec_pearl_gate(S.v, i, gb);
                                if (!na->books_set)     /* the attempt's first step: its books start here */
                                {
                                    memcpy(na->why[0], pn.why, sizeof na->why[0]);
                                    memcpy(na->whom[0], pn.whom, sizeof na->whom[0]);
                                    memcpy(na->gate[0], gb, sizeof gb);
                                    nl_ob(na->ob[0], &pn);
                                    na->books_set = 1;
                                }
                                memcpy(na->why[1], pn.why, sizeof na->why[1]);
                                memcpy(na->whom[1], pn.whom, sizeof na->whom[1]);
                                memcpy(na->gate[1], gb, sizeof gb);
                                nl_ob(na->ob[1], &pn);
                            }
                            if (fp > na->fp_last) na->fp_gain += fp - na->fp_last;
                            else na->fp_spent += na->fp_last - fp;
                            na->fp_last = fp;
                            for (int k = 0; k < w.p.st.nvis && k < NETH_RL_VIS; ++k)
                                if (w.p.st.vis[k].kind >= 0 && w.p.st.vis[k].kind <= DT_FOOD_LAST)
                                    nl_add_id(na->food_id, &na->nfood_seen, w.p.st.vis[k].id);
                            if (w.p.health < na->hmin) na->hmin = w.p.health;
                            if (na->ek_at < 0) na->ek_at = ek;
                            if (pn.on && pn.phase >= 0 && pn.phase < 12) ++na->phase[pn.phase];
                            uint32_t nmb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                            ++na->own[nmb ? __builtin_ctz(nmb) : 32];
                            if (nf && w.p.dim == 0)
                            {
                                na->view_steps += nf->n > 0;
                                for (int k = 0; k < nf->n && k < NETH_RL_FAR; ++k)
                                {
                                    if (nl_add_id(na->seen_id, &na->nseen, nf->e[k].id))
                                    {
                                        double fd = sqrt((nf->e[k].x - w.p.x) * (nf->e[k].x - w.p.x) +
                                                         (nf->e[k].y + 0.5 * nf->e[k].h - w.p.y - 1.62) *
                                                         (nf->e[k].y + 0.5 * nf->e[k].h - w.p.y - 1.62) +
                                                         (nf->e[k].z - w.p.z) * (nf->e[k].z - w.p.z));
                                        ++na->fs_ph[pn.on && pn.phase >= 0 && pn.phase < 12 ? pn.phase : 12];
                                        ++na->fs_d[fd < 8.0 ? 0 : fd < 16.0 ? 1 : fd < 32.0 ? 2 : 3];
                                        if (nf->census.on)
                                        {
                                            na->fs_st[0] += nf->census.pl_sky != 0;
                                            na->fs_st[1] += nf->census.ground_dy >= 2.0f;
                                        }
                                        na->fs_st[2] += w.p.y < 62.0;
                                    }
                                    if (nf->e[k].scream) nl_add_id(na->scream_id, &na->nscream, nf->e[k].id);
                                }
                                if (nf->census.on && night && w.p.health > 0.0f)
                                {
                                    na->night_steps += 1;
                                    struct neth_rl_tower tw;
                                    if (neth_rl_tower(S.h, i, &tw) == 0)
                                        for (int b = 0; b < 2; ++b)
                                        {
                                            na->tw_n[b] += tw.n[b];
                                            for (int j = 0; j < NETH_RL_TOWER_NH; ++j)
                                                na->tw_c[b][j] += tw.clear[b][j], na->tw_r[b][j] += tw.reach[b][j];
                                        }
                                    for (int k = 0; k < nf->census.n && k < NETH_RL_CENSUS; ++k)
                                    {
                                        double h2 = (double)nf->census.e[k].dx * nf->census.e[k].dx +
                                                    (double)nf->census.e[k].dz * nf->census.e[k].dz;
                                        na->alive128 += 1;
                                        na->alive64 += h2 <= 64.0 * 64.0;
                                        na->nr[8] += nf->census.e[k].sky != 0;
                                        if (h2 <= 64.0 * 64.0)
                                        {
                                            float dy = nf->census.e[k].dy;
                                            ++na->nr[dy < -6.0f ? 0 : dy <= 6.0f ? 1 : 2];
                                            na->nr[3] += nf->census.e[k].sky != 0;
                                            na->nr[4] += nf->census.e[k].clear != 0;
                                            na->nr[5] += nf->census.e[k].clear4 != 0;
                                            na->nr[6] += nf->census.e[k].clear8 != 0;
                                            na->nr[7] += nf->census.e[k].hi != 0;
                                        }
                                    }
                                }
                            }
                        }
                        e->hunt_night += pn.on && night && w.p.dim == 0;
                        e->ender_view += pn.nfar > 0 && w.p.dim == 0;
                        {
                            const struct neth_rl_far *cf = neth_rl_far(S.h, i);
                            if (cf && e->ek0 < 0) e->ek0 = cf->ender_kills;
                            if (cf && cf->census.on && night && w.p.dim == 0 && w.p.health > 0.0f)
                            {
                                e->night_steps += 1;
                                for (int k = 0; k < cf->census.n && k < NETH_RL_CENSUS; ++k)
                                {
                                    double h2 = (double)cf->census.e[k].dx * cf->census.e[k].dx +
                                                (double)cf->census.e[k].dz * cf->census.e[k].dz;
                                    e->alive128 += 1;
                                    e->alive64 += h2 <= 64.0 * 64.0;
                                }
                            }
                        }
                        if (dtrace_out || ktrace_out)
                        {
                            if (!e->dt) e->dt = calloc(DT_RING, sizeof *e->dt);
                            struct dt_row *q = &e->dt[e->dt_n++ % DT_RING];
                            const struct neth_rl_far *far = neth_rl_far(S.h, i);
                            int cs = w.p.st.current >= 0 && w.p.st.current < 9 ? w.p.st.current : 0;
                            memset(q, 0, sizeof *q);
                            q->step = e->n, q->wt = wt;
                            q->mbit = b.label ? (uint32_t)b.label[i].mbit : 0u;
                            q->kind = (int8_t)w.p.kind, q->pon = (int8_t)pn.on, q->phase = (int8_t)pn.phase;
                            q->attack = (int8_t)b.acts[i].press[PK_ATTACK], q->fire = (int8_t)(w.p.fire > 0);
                            q->prov = (int8_t)neth_vec_prov_mode(S.v, i);
                            q->x = (float)w.p.x, q->y = (float)w.p.y, q->z = (float)w.p.z, q->health = w.p.health;
                            q->food = (int16_t)w.p.food, q->food_pts = (int16_t)food_points(&w.p.st);
                            q->armor = (int16_t)w.armor;
                            q->held = (int16_t)(w.p.st.inv[cs].count > 0 ? w.p.st.inv[cs].item : 0);
                            q->tid_angry = (int16_t)pn.angry, q->tdist = pn.tdist, q->nfar = (int16_t)pn.nfar;
                            q->vskel = -1.0f, q->yaw = w.p.yaw, q->pitch = w.p.pitch;
                            q->mo_kind = (int16_t)w.p.st.mo_ekind;
                            q->vkind = -1, q->vdist = -1.0f;
                            {
                                struct neth_rl_combat_now cb;
                                neth_rl_combat(S.h, i, &cb);
                                q->cm_dt = (int16_t)cb.dt, q->cm_name = (int16_t)cb.name;
                                q->cm_damage = cb.damage, q->cm_fall = cb.fall_len, q->cm_age = cb.age;
                            }
                            q->hold = b.acts[i].hold;
                            q->tid = pn.tid, q->mo_ent = pn.cause[2], q->tvis = pn.tvis, q->thit_age = pn.thit_age;
                            q->ekills = far ? far->ender_kills : -1;
                            if (pn.tid)
                            {
                                double tdx = pn.tx - w.p.x, tdz = pn.tz - w.p.z, tdy = pn.ty + 1.45 - (w.p.y + 1.62);
                                double tyaw = atan2(-tdx, tdz) * 180.0 / M_PI;
                                double bear = fmod(tyaw - w.p.yaw, 360.0);
                                if (bear > 180.0) bear -= 360.0;
                                if (bear < -180.0) bear += 360.0;
                                q->tbear = (float)bear;
                                q->tpit = (float)(-atan2(tdy, sqrt(tdx * tdx + tdz * tdz)) * 180.0 / M_PI - w.p.pitch);
                            }
                            for (int k = 0; k < w.p.st.nvis && k < NETH_RL_VIS; ++k)
                            {
                                double ddx = w.p.st.vis[k].x - w.p.x, ddz = w.p.st.vis[k].z - w.p.z;
                                double ddy = w.p.st.vis[k].y + 0.5 * w.p.st.vis[k].h - (w.p.y + 1.62);
                                float dd = (float)sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
                                if (w.p.st.vis[k].kind == DT_SKELETON && (q->vskel < 0.0f || dd < q->vskel)) q->vskel = dd;
                                if (w.p.st.vis[k].kind > DT_FOOD_LAST && (q->vdist < 0.0f || dd < q->vdist))
                                    q->vkind = (int16_t)w.p.st.vis[k].kind, q->vdist = dd;
                            }
                            q->censon = (int8_t)(far && far->census.on);
                            if (q->censon)
                            {
                                const struct neth_rl_census *c = &far->census;
                                q->sky = (int8_t)c->pl_sky, q->ground = c->ground_dy, q->skel_los = (int16_t)c->skel_los;
                                q->onground = (int8_t)(c->ground_dy >= 0.0f && c->ground_dy < 0.01f);
                                for (int k = 0; k < 4; ++k) q->hostile[k] = (int16_t)c->hostile[k];
                                for (int k = 0; k < 5; ++k) q->hnear[k] = c->hnear[k];
                            }
                            /* lane/pground: --kill-trace's dump at each rise of the stats' endermen killed */
                            if (ktrace_out && q->ekills >= 0)
                            {
                                if (e->kt_set && q->ekills > e->kt_ek) dt_dump(ktrace_out, i, e->dt, e->dt_n);
                                e->kt_ek = q->ekills, e->kt_set = 1;
                            }
                        }
                    }
                    if (flog_out && w.p.dim == 0 && w.p.health > 0.0f)
                    {
                        /* lane/pground (--floor-log, read only, PRIVILEGED): the floor box's half-day books */
                        if (!e->fl) e->fl = calloc(1, sizeof *e->fl);
                        struct fl_acc *g = e->fl;
                        int64_t wt = w.p.world_time % 24000;
                        int nt = wt >= 13000 && wt < 23000;
                        int held = inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE);
                        const struct neth_rl_far *nf = neth_rl_far(S.h, i);
                        int ek = nf ? nf->ender_kills : -1;
                        if (!g->set) fl_begin(g, nt, held, ek), g->hx = w.p.x, g->hz = w.p.z;
                        else if (nt != g->night)
                        {
                            fl_row(flog_out, i, g, "half", held, ek);
                            fl_crow(flog_out, i, g);
                            fl_begin(g, nt, held, ek);
                        }
                        g->steps += 1;
                        g->home += hypot(w.p.x - g->hx, w.p.z - g->hz) <= 1.0;
                        if (nf && nf->near.id && !fl_has(g->con, g->ncon, nf->near.id) && g->ncon < FL_IDS)
                            g->con[g->ncon++] = nf->near.id;
                        for (int k = 0; nf && k < nf->n && k < NETH_RL_FAR; ++k)
                            if (nf->e[k].scream && !fl_has(g->prov, g->nprov, nf->e[k].id) && g->nprov < FL_IDS)
                                g->prov[g->nprov++] = nf->e[k].id;
                        if (g->steps % FL_EVERY == 1)
                        {
                            struct neth_rl_floor fb;
                            if (neth_rl_floor(S.h, i, fl_box, (!g->lit) | 2, &fb) == 0)
                            {
                                for (int k = 0; k < NETH_RL_FLOOR_K; ++k)
                                    g->ck[k][0] += fb.kind[k][0], g->ck[k][1] += fb.kind[k][1];
                                for (int k = 0; k < 4; ++k) g->cb[k][0] += fb.band[k][0], g->cb[k][1] += fb.band[k][1];
                                if (g->primed)
                                {
                                    for (int k = 0; k < g->pn; ++k)
                                        if (!fl_has(fb.mid, fb.mn, g->pid[k])) g->left[g->ptick[k] ? 1 : 0] += 1;
                                    for (int k = 0; k < fb.mn; ++k)
                                        if (!fl_has(g->pid, g->pn, fb.mid[k])) g->came += 1;
                                }
                                /* the first sample of the attempt is the base (the counters run from the
                                 * spawner's init); a counter under the base (a dev purge's re-init) starts over */
                                if (!g->gprimed) memcpy(g->gbase, fb.gate, sizeof fb.gate), g->gprimed = 1;
                                for (int k = 0; k < 4; ++k)
                                    if (fb.gate[k] < g->gbase[k]) g->gbase[k] = 0;
                                memcpy(g->glast, fb.gate, sizeof fb.gate);
                                g->pn = fb.mn, g->primed = 1;
                                memcpy(g->pid, fb.mid, sizeof(int32_t) * (size_t)fb.mn);
                                memcpy(g->ptick, fb.mtick, (size_t)fb.mn);
                                if (!g->lit)
                                    g->lit = 1, g->sky_max = fb.sky_max, g->blk_max = fb.blk_max, g->cells = fb.cells,
                                    g->spawnable = fb.spawnable;
                                g->samples += 1, g->capn += fb.capn, g->cap += fb.cap;
                                g->mon_steps += (int64_t)FL_EVERY * fb.all;
                                for (int k = 0; k < fb.n; ++k)
                                {
                                    if (fb.e[k].ender) g->ender_steps += FL_EVERY;
                                    if (fl_has(g->id, g->nid, fb.e[k].id) || g->nid >= FL_IDS) continue;
                                    g->id[g->nid] = fb.e[k].id, g->ender[g->nid] = (unsigned char)fb.e[k].ender;
                                    g->born[g->nid] = fb.e[k].age <= FL_BORN;
                                    g->nid += 1;
                                }
                            }
                        }
                    }
                    if (lvlog_out)
                    {
                        /* lane/lava (--lava-log, books only, read only) */
                        if (!e->lv) e->lv = calloc(LV_RING, sizeof *e->lv);
                        struct lv_row *q = &e->lv[e->lv_n++ % LV_RING];
                        struct neth_vec_lava ls;
                        memset(q, 0, sizeof *q);
                        if (neth_vec_lava(S.v, i, &ls) != 0) memset(&ls, 0, sizeof ls), ls.hot_d = -1.0f;
                        const struct neth_rl_state *st = &w.p.st;
                        int cs = st->current >= 0 && st->current < 9 ? st->current : 0;
                        int fx = (int)floor(w.p.x), fy = (int)floor(w.p.y + 0.01), fz = (int)floor(w.p.z);
                        q->step = e->n, q->lava_t = ls.lava_t >= 0 ? ls.steps - ls.lava_t : -1;
                        q->mbit = b.label ? (uint32_t)b.label[i].mbit : 0u, q->hold = b.acts[i].hold;
                        q->kind = (int8_t)w.p.kind, q->lsrc = (int8_t)(ls.lsrc & 0x7f);
                        q->fire = (int8_t)(w.p.fire > 0), q->og = (int8_t)ls.on_ground;
                        q->fire_t = (int16_t)(w.p.fire > 32767 ? 32767 : w.p.fire), q->water_d = ls.water_d;
                        q->eye_water = (int8_t)st->eye_water;
                        q->edge = (int8_t)ls.edge, q->st_hot = (int8_t)ls.st_hot, q->st_near = (int8_t)ls.st_near;
                        q->ob_hot = (int8_t)ls.ob_hot, q->esc = (int8_t)ls.esc, q->sdir = (int8_t)ls.sdir;
                        q->press_att = (int8_t)b.acts[i].press[PK_ATTACK], q->press_use = (int8_t)b.acts[i].press[PK_USE];
                        q->hotbar = (int8_t)b.acts[i].hotbar;
                        q->x = (float)w.p.x, q->y = (float)w.p.y, q->z = (float)w.p.z, q->yaw = w.p.yaw;
                        q->pitch = w.p.pitch, q->health = w.p.health, q->hot_d = ls.hot_d, q->hot_n = (int16_t)ls.hot_n;
                        q->held = (int16_t)(st->inv[cs].count > 0 ? st->inv[cs].item : 0);
                        q->mo_hit = (int16_t)st->mo_hit, q->mo_block = (int16_t)(st->mo_hit ? st->mo_block : -1);
                        q->mo_dx = (int16_t)(st->mo_x - fx), q->mo_dy = (int16_t)(st->mo_y - fy);
                        q->mo_dz = (int16_t)(st->mo_z - fz);
                        q->lq_hit = (int16_t)st->lq_hit, q->lq_block = (int16_t)(st->lq_hit ? st->lq_block : -1);
                        {
                            double ldx = st->lq_x + 0.5 - w.p.x, ldy = st->lq_y + 0.5 - (w.p.y + 1.62);
                            double ldz = st->lq_z + 0.5 - w.p.z;
                            q->lq_d = st->lq_hit ? (float)sqrt(ldx * ldx + ldy * ldy + ldz * ldz) : -1.0f;
                        }
                        /* PRIVILEGED: the client world's box round the feet */
                        enum { LW = 2 * LV_BOX + 1, LH = 2 * LV_BOX + 2 };
                        int16_t bx[LW * LW * LH];
                        q->b_feet = q->b_head = q->b_under = -1, q->lv_dx = q->lv_dy = q->lv_dz = 0;
                        if (w.p.dim == 0 || w.p.dim == -1)
                            if (neth_rl_blocks(S.h, i, fx - LV_BOX, fy - LV_BOX, fz - LV_BOX, fx + LV_BOX, fy + LV_BOX + 1,
                                            fz + LV_BOX, bx) == 0)
                            {
#define LVB(X, Y, Z) bx[((Y) + LV_BOX) * LW * LW + ((Z) + LV_BOX) * LW + ((X) + LV_BOX)]
#define LVL(X, Y, Z) (LVB(X, Y, Z) == 10 || LVB(X, Y, Z) == 11)
                                q->b_feet = LVB(0, 0, 0), q->b_head = LVB(0, 1, 0), q->b_under = LVB(0, -1, 0);
                                static const int F[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
                                int touch = LVL(0, 0, 0) + LVL(0, 1, 0) + LVL(0, -1, 0) + LVL(0, 2, 0);
                                for (int k = 0; k < 4; ++k)
                                    touch += LVL(F[k][0], 0, F[k][2]) + LVL(F[k][0], 1, F[k][2]);
                                q->lv_touch = (int16_t)touch;
                                int best = 1 << 30;
                                for (int dy = -LV_BOX; dy <= LV_BOX + 1; ++dy)
                                    for (int dz = -LV_BOX; dz <= LV_BOX; ++dz)
                                        for (int dx = -LV_BOX; dx <= LV_BOX; ++dx)
                                        {
                                            if (!LVL(dx, dy, dz)) continue;
                                            int ch = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
                                            int cy = dy < 0 ? -dy : dy > 1 ? dy - 1 : 0;
                                            ch = ch > cy ? ch : cy;
                                            q->lv_box += 1, q->lv_1 += ch <= 1;
                                            int d2 = dx * dx + dz * dz + (dy - (dy > 0)) * (dy - (dy > 0));
                                            if (d2 < best) best = d2, q->lv_dx = (int16_t)dx, q->lv_dy = (int16_t)dy,
                                                           q->lv_dz = (int16_t)dz;
                                        }
                                if (st->mo_hit)
                                {
                                    int mx = st->mo_x - fx, my = st->mo_y - fy, mz = st->mo_z - fz, nb = 0;
                                    for (int k = 0; k < 6; ++k)
                                    {
                                        int nx = mx + F[k][0], ny = my + F[k][1], nz = mz + F[k][2];
                                        if (abs(nx) <= LV_BOX && abs(nz) <= LV_BOX && ny >= -LV_BOX && ny <= LV_BOX + 1)
                                            nb += LVL(nx, ny, nz);
                                    }
                                    q->mo_lavanb = (int16_t)nb;
                                }
#undef LVL
#undef LVB
                            }
                    }
                    struct neth_rl_dig dg;
                    int dg_ok = 0;
                    if ((dlog_out || slog_out) && w.p.dim == 0 && w.p.health > 0.0f)
                        dg_ok = neth_rl_dig(S.h, i, &dg) == 0;
                    if (slog_out && w.p.dim == 0 && w.p.health > 0.0f)
                    {
                        /* lane/pdig (--sight-log, read only): the endermen seen in view and how each ended */
                        if (!e->sg) e->sg = calloc(1, sizeof *e->sg), e->sg->day_last = -1;
                        struct sg_acc *g = e->sg;
                        const struct neth_rl_far *nf = neth_rl_far(S.h, i);
                        int ek = nf ? nf->ender_kills : 0;
                        int64_t day = (w.p.world_time + 1000) / 24000;
                        int dawn = g->day_last >= 0 && day > g->day_last;
                        g->day_last = day;
                        struct neth_vec_pearl ps;
                        if (neth_vec_pearl(S.v, i, 1, &ps) != 0) memset(&ps, 0, sizeof ps);
                        for (int k = 0; nf && k < nf->n && k < NETH_RL_FAR; ++k)
                        {
                            int si = 0;
                            while (si < g->n && g->e[si].id != nf->e[k].id) ++si;
                            double fdx = nf->e[k].x - w.p.x, fdz = nf->e[k].z - w.p.z;
                            double fdy = nf->e[k].y + 0.5 * nf->e[k].h - (w.p.y + 1.62);
                            float fd = (float)sqrt(fdx * fdx + fdy * fdy + fdz * fdz);
                            if (si == g->n)
                            {
                                if (g->n == SG_N) continue;
                                struct sg_ent *s = &g->e[g->n++];
                                memset(s, 0, sizeof *s);
                                s->id = nf->e[k].id, s->first = e->n, s->wt0 = w.p.world_time % 24000, s->d0 = fd;
                                s->ph = ps.on && ps.phase >= 0 && ps.phase < 12 ? ps.phase : 12;
                                s->mb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                                s->scream = -1, s->dmin = fd, s->last_alive = e->n, s->ek_alive = ek;
                            }
                            struct sg_ent *s = &g->e[si];
                            s->view_steps += 1, s->last_view = e->n;
                            if (fd < s->dmin) s->dmin = fd;
                            if (nf->e[k].scream && s->scream < 0) s->scream = e->n;
                        }
                        for (int si = 0; si < g->n;)
                        {
                            struct sg_ent *s = &g->e[si];
                            int alive = 0;
                            for (int k = 0; dg_ok && k < dg.n; ++k)
                                if (dg.e[k].id == s->id)
                                {
                                    alive = 1;
                                    s->dend = dg.e[k].d, s->sky = dg.e[k].sky;
                                }
                            if (dawn) s->dawn = 1;
                            /* (F) the fight's books */
                            for (int k = 0; dg_ok && k < dg.n; ++k)
                                if (dg.e[k].id == s->id)
                                {
                                    float x = (float)(w.p.x + dg.e[k].dx), y = (float)(w.p.y + dg.e[k].dy),
                                          z = (float)(w.p.z + dg.e[k].dz);
                                    if (s->has_e && (x - s->ex) * (x - s->ex) + (y - s->ey) * (y - s->ey) +
                                                            (z - s->ez) * (z - s->ez) >= 16.0f)
                                        s->tp += 1, s->tp_last = e->n;
                                    s->ex = x, s->ey = y, s->ez = z, s->has_e = 1;
                                }
                            if (s->scream == e->n)
                            {
                                s->h0 = s->h1 = ps.hits, s->hp_s = s->hp_min = s->hp_prev = w.p.health;
                                s->ph_s = ps.on && ps.phase >= 0 && ps.phase < 12 ? ps.phase : 12;
                                s->sky_s = dg_ok ? dg.pl_sky : -1;
                            }
                            else if (s->scream >= 0)
                            {
                                if (ps.hits > s->h1) s->hit_last = e->n;
                                s->h1 = ps.hits;
                                if (w.p.health < s->hp_min) s->hp_min = w.p.health;
                                if (w.p.health < s->hp_prev) s->hurt += s->hp_prev - w.p.health;
                                s->hp_prev = w.p.health;
                            }
                            if (s->last_view == e->n) s->lv_x = (float)w.p.x, s->lv_z = (float)w.p.z, s->mv_after = 0;
                            else
                            {
                                float mx = (float)w.p.x - s->lv_x, mz = (float)w.p.z - s->lv_z;
                                float m = sqrtf(mx * mx + mz * mz);
                                if (m > s->mv_after) s->mv_after = m;
                            }
                            s->mb_e = b.label ? (uint32_t)b.label[i].mbit : 0u;
                            s->ph_e = ps.on && ps.phase >= 0 && ps.phase < 12 ? ps.phase : 12;
                            const char *why = NULL;
                            if (alive) s->last_alive = e->n, s->ek_alive = ek, s->miss = 0;
                            else if (dg_ok && dg.all <= dg.n && ++s->miss >= SG_MISS)
                                why = ek > s->ek_alive ? "killed" : "gone", s->dend = -1;
                            if (!why && alive && e->n - s->last_view > SG_LOST) why = "lost";
                            if (why)
                            {
                                sg_row(slog_out, i, s, why);
                                g->e[si] = g->e[--g->n];
                            }
                            else ++si;
                        }
                    }
                    if (dlog_out && w.p.dim == 0 && w.p.health > 0.0f)
                    {
                        /* lane/pdig (--dig-log, read only, PRIVILEGED): the visits within 48 and the attempt's counts */
                        if (!e->dg) e->dg = calloc(1, sizeof *e->dg), e->dg->day0 = -1;
                        struct dg_acc *g = e->dg;
                        int64_t wt = w.p.world_time % 24000;
                        int nt = wt >= 13000 && wt < 23000;
                        if (!dg_ok) dg.n = 0, dg.pl_sky = 0;
                        {
                            /* the N rows: the period's night steps (as --night-log's k) */
                            int64_t day = (w.p.world_time + 1000) / 24000;
                            if (g->day0 < 0) g->day0 = day, g->nk = 1, dg_nbegin(g);
                            for (; day > g->day0 + g->nk - 1; ++g->nk)
                            {
                                dg_nrow(dlog_out, i, g, "dawn");
                                dg_nbegin(g);
                            }
                            double mv = g->has_last ? hypot(w.p.x - g->lx, w.p.z - g->lz) : 0.0;
                            g->lx = w.p.x, g->lz = w.p.z, g->has_last = 1;
                            {
                                /* the K rows: the step's walk primitive by context */
                                int32_t wk[6];
                                int kls = neth_vec_lsrc(S.v, i);
                                uint32_t kmb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                                if (mv < 8.0 && neth_vec_walk(S.v, i, wk) == 0)
                                {
                                    int c = kmb == NETH_VEC_FOOD ? 0 : (kls >= 0 && (kls & 255) >= 8 && (kls & 255) < 24 &&
                                                                         (kls & 7) == VLP_WOOD) ? 1 :
                                            kmb == NETH_VEC_PEARL ? 2 : 3;
                                    int k = wk[0] & 63, mb = mv < 0.01 ? 0 : mv < 0.1 ? 1 : 2;
                                    if (k > WK_FOS)     /* the straight walk: 48 + its reason - 1 + 4 x what walk_to
                                                         * said before it (0 not asked, 1 no path, 2 there, 3 other) */
                                        k = WK_FOS + (k - WK_FOS - 1) +
                                            4 * (wk[3] == WK_WT + 3 ? 1 : wk[3] == WK_WT + 6 ? 2 : wk[3] ? 3 : 0),
                                        wk[3] = 0;
                                    ++g->k_n[c][nt][k][mb];
                                    if (mb < 2) ++g->k_f[c][nt][k][wk[4] >= 0 && wk[4] < 11 ? wk[4] : 0];
                                    int kc = k == WK_SHO + 12 ? 0 : k == WK_SHO + 13 ? 1 : k == WK_OTH + 1 ? 2 : -1;
                                    if (mb < 2 && kc >= 0) ++g->k_c[c][nt][kc][wk[5] & 127];
                                    if (!mb)
                                    {
                                        ++g->k_d[c][nt][k][wk[1] < 16 ? 0 : wk[1] < 32 ? 1 : wk[1] < 48 ? 2 : wk[1] < 64 ? 3 : 4];
                                        g->k_dy[c][nt][k] += wk[2], g->k_len[c][nt][k] += wk[3];
                                    }
                                }
                            }
                            {
                                /* the W rows: the books' rises this step (every living overworld step keeps the
                                 * last; a night step counts them) */
                                struct neth_vec_pearl pq;
                                int32_t gq[3] = {0, 0, 0}, oq[9];
                                if (neth_vec_pearl(S.v, i, 1, &pq) != 0) memset(&pq, 0, sizeof pq);
                                neth_vec_pearl_gate(S.v, i, gq);
                                nl_ob(oq, &pq);
                                if (!g->wset)
                                {
                                    memcpy(g->why_l, pq.why, sizeof g->why_l), memcpy(g->gate_l, gq, sizeof gq);
                                    memcpy(g->ob_l, oq, sizeof oq);
                                    g->wset = 1;
                                }
                                if (nt)
                                {
                                    int m = mv >= 0.1;
                                    uint32_t wmb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                                    ++g->w_own[wmb ? __builtin_ctz(wmb) : 32][m];
                                    ++g->w_ph[pq.on && pq.phase >= 0 && pq.phase < 12 ? pq.phase : 12][m];
                                    int ls = neth_vec_lsrc(S.v, i);
                                    if (ls >= 0 && (ls & 255) < 24) ++g->w_src[ls & 255][m];
                                    if (ls >= 0 && ((ls >> 8) & 15) > 0) ++g->w_wood[(ls >> 8) & 15][m];
                                    if (ls >= 0 && ((ls >> 16) & 31) > 0) ++g->w_idle[(ls >> 16) & 31][m];
                                    if (ls >= 0 && (ls & 255) >= 8 && (ls & 255) < 24)
                                        ++g->w_pvx[ls & 255][(ls >> 12) & 15][m];
                                    for (int k = 0; k < 12; ++k) g->w_why[k][m] += pq.why[k] - g->why_l[k];
                                    for (int k = 0; k < 3; ++k) g->w_gate[k][m] += gq[k] - g->gate_l[k];
                                    for (int k = 0; k < 9; ++k)
                                    {
                                        if (!m) g->w_ob[k][0] += oq[k] - g->ob_l[k];
                                        g->w_ob[k][1] += oq[k] - g->ob_l[k];
                                    }
                                }
                                memcpy(g->why_l, pq.why, sizeof g->why_l), memcpy(g->gate_l, gq, sizeof gq);
                                memcpy(g->ob_l, oq, sizeof oq);
                            }
                            {
                                /* the D rows (F1): the day's food */
                                struct neth_vec_food fq;
                                if (neth_vec_food(S.v, i, &fq) == 0)
                                {
                                    if (!g->dset) g->d_fb0 = fq, g->dset = 1;
                                    else
                                        for (int k = 0; k < 6; ++k)
                                            if (fq.scout_end[k] > g->d_fbl.scout_end[k])
                                            {
                                                /* (F2c) a scout ended this step */
                                                const struct neth_rl_far *sf = neth_rl_far(S.h, i);
                                                g->d_sc[k][0] += fq.scout_end[k] - g->d_fbl.scout_end[k];
                                                g->d_sc[k][1] += fq.scout_far - g->d_fbl.scout_far;
                                                if (sf && sf->census.on && sf->census.food_n > 0)
                                                {
                                                    g->d_sc[k][2] += 1;
                                                    g->d_sc[k][3] += (int64_t)hypot(sf->census.food_x - w.p.x,
                                                                                    sf->census.food_z - w.p.z);
                                                    g->d_sc[k][4] += (int64_t)(w.p.y - sf->census.food_y);
                                                }
                                            }
                                    g->d_fbl = fq;
                                }
                                if (!nt)
                                {
                                    int fl = w.p.food, fp = food_points(&w.p.st);
                                    int raw = inv_n(&w.p.st, IT_PORK) + inv_n(&w.p.st, IT_BEEF) +
                                              inv_n(&w.p.st, IT_CHICKEN);
                                    int ck = inv_n(&w.p.st, IT_PORK_C) + inv_n(&w.p.st, IT_BEEF_C) +
                                             inv_n(&w.p.st, IT_CHICKEN_C);
                                    if (!g->dseen) g->d_f0 = fl, g->d_fp0 = fp, g->d_raw0 = raw, g->d_ck0 = ck, g->dseen = 1;
                                    g->d_f1 = fl, g->d_fp1 = fp, g->d_raw1 = raw, g->d_ck1 = ck;
                                    g->d_steps += 1;
                                    int ls = neth_vec_lsrc(S.v, i);
                                    int bd = fl >= 14 ? 0 : fl >= 7 ? 1 : 2;
                                    if (ls >= 0 && (ls & 255) < 24) ++g->d_bs[bd][ls & 255];
                                    if (ls >= 0 && (ls & 255) >= 8 && ((ls >> 12) & 15) < 10)
                                        ++g->d_pv[(ls >> 12) & 15][w.p.y >= 62.0][dg.pl_sky != 0];
                                    uint32_t dmb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                                    ++g->d_own[dmb ? __builtin_ctz(dmb) : 32];
                                    if (dmb == NETH_VEC_FOOD) ++g->d_fmv[mv >= 0.1];
                                    const struct neth_rl_far *ff = neth_rl_far(S.h, i);
                                    if (ff && ff->census.on && ff->census.food_n > 0)
                                    {
                                        g->d_an += ff->census.food_n, g->d_any += 1;
                                        double fd = hypot(ff->census.food_x - w.p.x, ff->census.food_z - w.p.z);
                                        ++g->d_near[fd < 16.0 ? 0 : fd < 32.0 ? 1 : fd < 64.0 ? 2 : 3];
                                    }
                                }
                            }
                            if (nt)
                            {
                                g->nsteps += 1;
                                g->nsky += dg.pl_sky != 0;
                                g->nmove += mv >= 0.1;
                                if (mv < 8.0) g->dist += mv;    /* (a teleport or a respawn not counted) */
                                {
                                    /* the G rows (lane/pground): the ground and the clear lines */
                                    struct neth_rl_ground gd;
                                    if (neth_rl_ground(S.h, i, g->gr_n % GR_EVERY == 0 ? 7 : 0, &gd) == 0)
                                    {
                                        if (g->gr_n % GR_EVERY == 0)
                                        {
                                            g->gr_cn += 1;
                                            for (int k = 0; k < NETH_RL_GR_K; ++k) g->gr_c[k] += gd.cells[k];
                                            /* the A rows: the render distance's world */
                                            g->a_n += 1, g->a_capn += gd.capn, g->a_cap += gd.cap, g->a_view = gd.view;
                                            if (gd.capn > g->a_capmax) g->a_capmax = gd.capn;
                                            g->a_load += gd.nload, g->a_loadw += gd.nload_w, g->a_lmax += gd.lmax;
                                            for (int u = 0; u < 2; ++u)
                                                for (int t = 0; t < 2; ++t)
                                                    g->a_mon[u][t] += gd.mon[u][t], g->a_end[u][t] += gd.end[u][t];
                                            for (int k = 0; k < gd.n; ++k)
                                            {
                                                if (gd.e[k].dead || gd.e[k].d > 64.0f) continue;
                                                int u = gd.e[k].tick != 0, t = gd.e[k].watch != 0;
                                                ++g->a_e64[u][t];
                                                if (!gd.e[k].clear) continue;
                                                g->a_cs[t] += gd.e[k].sky != 0;
                                                int q = 0;
                                                while (q < g->a_nid[t] && g->a_ids[t][q] != gd.e[k].id) ++q;
                                                if (q == g->a_nid[t] && q < 32) g->a_ids[t][g->a_nid[t]++] = gd.e[k].id;
                                            }
                                            /* the O rows: an underground sample by its openness */
                                            if (!gd.pl_sky)
                                            {
                                                int ob = op_band(gd.air8);
                                                struct neth_vec_pearl opq;
                                                if (neth_vec_pearl(S.v, i, 1, &opq) != 0) memset(&opq, 0, sizeof opq);
                                                uint32_t omb = b.label ? (uint32_t)b.label[i].mbit : 0u;
                                                ++g->o_n[ob], ++g->o_own[ob][omb ? __builtin_ctz(omb) : 32];
                                                ++g->o_ph[ob][opq.on && opq.phase >= 0 && opq.phase < 12 ? opq.phase : 12];
                                                for (int k = 0; k < gd.n; ++k)
                                                {
                                                    if (gd.e[k].dead || gd.e[k].d > 64.0f) continue;
                                                    ++g->o_es[ob];
                                                    if (!gd.e[k].clear) continue;
                                                    ++g->o_ec[ob];
                                                    int q = 0;
                                                    while (q < g->o_nid[ob] && g->o_ids[ob][q] != gd.e[k].id) ++q;
                                                    if (q == g->o_nid[ob] && q < OP_IDS) g->o_ids[ob][g->o_nid[ob]++] = gd.e[k].id;
                                                }
                                            }
                                        }
                                        int64_t now = g->gr_n;
                                        g->gr_n += 1, g->gr_y += gd.y, g->gr_low += gd.y < 62.0f;
                                        ++g->gr_bio[gd.biome & 255];
                                        if (!g->gr_pos) g->gr_x0 = w.p.x, g->gr_z0 = w.p.z, g->gr_pos = 1;
                                        g->gr_x1 = w.p.x, g->gr_z1 = w.p.z;
                                        if (now % GR_POS_EVERY == 0 && g->gr_np < GR_POS)
                                            g->gr_p[g->gr_np][0] = (int32_t)floor(w.p.x),
                                            g->gr_p[g->gr_np][1] = (int32_t)floor(w.p.y),
                                            g->gr_p[g->gr_np][2] = (int32_t)floor(w.p.z), ++g->gr_np;
                                        g->gr_trunc += gd.all > gd.n;
                                        /* the E rows' trackers: seen this step within 128, beyond it, or gone */
                                        for (int t = 0; t < g->gr_ntr; ++t)
                                        {
                                            struct gr_tr *q = &g->gr_tr[t];
                                            if (q->gone) continue;
                                            int k = 0;
                                            while (k < gd.n && gd.e[k].id != q->id) ++k;
                                            if (k == gd.n) { q->gone = 1; continue; }
                                            q->dead = gd.e[k].dead, q->age1 = gd.e[k].age;
                                            q->out = gd.e[k].d > 128.0f;
                                        }
                                        for (int k = 0; k < gd.n; ++k)
                                        {
                                            if (gd.e[k].d > 128.0f) continue;
                                            int t = 0;
                                            while (t < g->gr_ntr && g->gr_tr[t].id != gd.e[k].id) ++t;
                                            if (t == g->gr_ntr)
                                            {
                                                if (gd.e[k].dead) continue;     /* (one first met dying is not counted) */
                                                if (t == GR_TR) { ++g->gr_trover; continue; }
                                                struct gr_tr *q = &g->gr_tr[g->gr_ntr++];
                                                memset(q, 0, sizeof *q);
                                                q->id = gd.e[k].id, q->age0 = q->age1 = gd.e[k].age, q->first = now;
                                            }
                                            struct gr_tr *q = &g->gr_tr[t];
                                            if (q->gone) continue;
                                            q->last = now, q->out = 0, q->dead = gd.e[k].dead, q->age1 = gd.e[k].age;
                                            if (!gd.e[k].dead)
                                            {
                                                q->reach |= gd.e[k].reach;
                                                q->reach64 |= gd.e[k].reach && gd.e[k].d <= 64.0f;
                                                if (gd.e[k].d <= 64.0f)
                                                {
                                                    int c = 2 * (gd.pl_sky != 0) + (gd.e[k].sky != 0);
                                                    if (!q->in64) q->first64 = now;
                                                    q->in64 = 1, q->last64 = now, q->cell = (int8_t)c;
                                                    ++q->s64[c], q->c64[c] += gd.e[k].clear;
                                                    ++g->q_st[c], g->q_cl[c] += gd.e[k].clear, g->q_rc[c] += gd.e[k].reach;
                                                }
                                            }
                                        }
                                        for (int k = 0; k < gd.n; ++k)
                                            for (int s = 0; s < 2; ++s)
                                            {
                                                if (gd.e[k].dead || gd.e[k].d > 64.0f) continue;
                                                if (!gd.e[k].clear || (s == 1 && !gd.e[k].sky)) continue;
                                                int q = 0;
                                                while (q < g->gr_nid[s] && g->gr_ids[s][q] != gd.e[k].id) ++q;
                                                if (q == g->gr_nid[s] && q < DG_IDS) g->gr_ids[s][g->gr_nid[s]++] = gd.e[k].id;
                                            }
                                    }
                                }
                                for (int k = 0; k < dg.n; ++k)
                                {
                                    float d = dg.e[k].d;
                                    for (int s = 0; s < 4; ++s)
                                    {
                                        if ((s & 1) == 0 && d > 64.0f) continue;
                                        if (s < 2 && !dg.e[k].sky) continue;
                                        int q = 0;
                                        while (q < g->nid[s] && g->ids[s][q] != dg.e[k].id) ++q;
                                        if (q == g->nid[s] && q < DG_IDS) g->ids[s][g->nid[s]++] = dg.e[k].id;
                                    }
                                }
                            }
                        }
                        ++g->steps[nt];
                        for (int k = 0; k < dg.n; ++k)
                        {
                            float d = dg.e[k].d, dy = dg.e[k].dy;
                            if (d > 48.0f) continue;    /* (the visits are within 48) */
                            int bmax = d <= 16.0f ? 2 : d <= 32.0f ? 1 : 0;
                            for (int bb = 0; bb <= bmax; ++bb) ++g->es[bb][nt];
                            if (bmax >= 1)
                            {
                                ++g->dyb[dy < -16.0f ? 0 : dy < -6.0f ? 1 : dy <= 6.0f ? 2 : 3][nt];
                                g->line[0][nt] += dg.e[k].cells[NETH_RL_DIG_LAVA] > 0;
                                g->line[1][nt] += dg.e[k].cells[NETH_RL_DIG_WATER] > 0;
                                g->line[2][nt] += dg.e[k].cells[NETH_RL_DIG_LOOSE] > 0;
                            }
                            int vi = 0;
                            while (vi < g->nv && g->v[vi].id != dg.e[k].id) ++vi;
                            if (vi == g->nv)
                            {
                                if (g->nv == DG_V) continue;    /* (full: not counted as a visit) */
                                struct dg_visit *nv = &g->v[g->nv++];
                                memset(nv, 0, sizeof *nv);
                                nv->id = dg.e[k].id, nv->wt0 = wt, nv->dmin = 1e9f;
                                for (int bb = 0; bb < 3; ++bb) nv->f[bb] = nv->l[bb] = -1;
                            }
                            struct dg_visit *v = &g->v[vi];
                            for (int bb = 0; bb <= bmax; ++bb)
                            {
                                if (v->f[bb] < 0)
                                {
                                    v->f[bb] = e->n;
                                    if (bb < 2)
                                    {
                                        int s = bb == 1 ? 0 : 1;   /* e[0]: at the first step within 32, e[1]: 48 */
                                        v->e[s][0] = dg.e[k].dx, v->e[s][1] = dg.e[k].dy, v->e[s][2] = dg.e[k].dz;
                                        v->sky[s] = dg.e[k].sky;
                                        memcpy(v->c[s], dg.e[k].cells, sizeof v->c[s]);
                                    }
                                }
                                v->l[bb] = e->n;
                                ++v->st[bb][nt];
                            }
                            if (d < v->dmin) v->dmin = d, v->dydmin = dy;
                        }
                        for (int vi = 0; vi < g->nv;)
                            if (e->n - g->v[vi].l[0] > DG_GAP)
                            {
                                dg_vrow(dlog_out, i, &g->v[vi], "gone");
                                g->v[vi] = g->v[--g->nv];
                            }
                            else ++vi;
                    }
                    if (ntrace_out && (ntrace_i < 0 || i == ntrace_i) && w.p.dim == -1)
                    {
                        /* --nether-trace (lane/legs): the step, place, look, health, owner, kind, the nether macro's
                         * books (phase, mode, fortress and spawner kept, the search's goal), the client world's block
                         * under the feet, at the feet and the head, and at the feet one block ahead along the facing */
                        struct neth_vec_nether nt = {0};
                        neth_vec_nether(S.v, i, &nt);
                        int fx = (int)floor(w.p.x), fy = (int)floor(w.p.y + 0.01), fz = (int)floor(w.p.z);
                        double yr = w.p.yaw * M_PI / 180.0;
                        int ax = (int)floor(w.p.x - sin(yr)), az = (int)floor(w.p.z + cos(yr));
                        /* lane/rods (PRIVILEGED, books only): blaze rods lying within 24 blocks of the feet, the nearest's
                         * distance and its height over the feet (-1, 0 none) */
                        struct netherview_item rgi[64];
                        int rgn = neth_vec_nether_items(S.v, i, rgi, 64), rg_n = 0;
                        if (rodlog_out)
                        {
                            /* lane/rods: the rod log: items new since the last step, and items gone since it */
                            int nn = rgn < 0 ? 0 : rgn < 32 ? rgn : 32;
                            for (int pass = 0; pass < 2; ++pass)
                            {
                                int na = pass ? nn : e->rl_n;
                                for (int q = 0; q < na; ++q)
                                {
                                    int id = pass ? rgi[q].id : e->rl[q].id, seen = 0;
                                    int nb = pass ? e->rl_n : nn;
                                    for (int u = 0; u < nb && !seen; ++u) seen = (pass ? e->rl[u].id : rgi[u].id) == id;
                                    if (seen) continue;
                                    float ix = pass ? rgi[q].x : e->rl[q].x, iy = pass ? rgi[q].y : e->rl[q].y;
                                    float iz = pass ? rgi[q].z : e->rl[q].z;
                                    int cx = (int)floor(ix), cy = (int)floor(iy), cz = (int)floor(iz);
                                    fprintf(rodlog_out, "%lld\t%d\t%s\t%d\t%d\t%d\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\n",
                                            (long long)e->n, i, pass ? "new" : "gone", id,
                                            pass ? rgi[q].age : e->rl[q].age, pass ? rgi[q].count : 0, ix, iy, iz,
                                            cx - fx, cy - fy, cz - fz, neth_vec_nether_block(S.v, i, cx, cy, cz),
                                            neth_vec_nether_block(S.v, i, cx, cy - 1, cz), nt.rods, nt.hood, w.p.health);
                                }
                            }
                            e->rl_n = nn;
                            for (int q = 0; q < nn; ++q)
                                e->rl[q].id = rgi[q].id, e->rl[q].age = rgi[q].age, e->rl[q].x = rgi[q].x,
                                e->rl[q].y = rgi[q].y, e->rl[q].z = rgi[q].z;
                        }
                        float rg_d = -1.0f, rg_dy = 0.0f;
                        for (int q = 0; q < rgn && q < 64; ++q)
                        {
                            float d = (float)sqrt((rgi[q].x - w.p.x) * (rgi[q].x - w.p.x) +
                                                  (rgi[q].y - w.p.y) * (rgi[q].y - w.p.y) +
                                                  (rgi[q].z - w.p.z) * (rgi[q].z - w.p.z));
                            if (d > 24.0f) continue;
                            rg_n += rgi[q].count;
                            if (rg_d < 0 || d < rg_d) rg_d = d, rg_dy = (float)(rgi[q].y - w.p.y);
                        }
                        /* lane/rods (books only): with a hood, its nine cells open in the client world (air): bit 0 the
                         * roof (feet + 2), bits 1..4 the head level's sides (+x -x +z -z), bits 5..8 the feet level's;
                         * and whether the feet stand in the hood's cell (-1 no hood) */
                        int hmask = -1, hin = -1;
                        if (nt.hood > 0)
                        {
                            static const int HD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                            int hx = nt.hood_at[0], hy = nt.hood_at[1], hz = nt.hood_at[2];
                            hmask = neth_vec_nether_block(S.v, i, hx, hy + 2, hz) == 0;
                            for (int q = 0; q < 4; ++q)
                            {
                                hmask |= (neth_vec_nether_block(S.v, i, hx + HD[q][0], hy + 1, hz + HD[q][1]) == 0) << (1 + q);
                                hmask |= (neth_vec_nether_block(S.v, i, hx + HD[q][0], hy, hz + HD[q][1]) == 0) << (5 + q);
                            }
                            hin = fx == hx && fz == hz && fy == hy;
                        }
                        /* lane/armed (books only): the fire (block 51) about the feet in the client world, the cells
                         * fx-1..fx+1, fy-1..fy+2, fz-1..fz+1 counted (fire_near), and the hood's nine cells holding fire
                         * as hmask's bits (hood_fire, -1 no hood): a shut hood's cell (hmask counts only air as open)
                         * may be a fire block */
                        int fire_near = 0, hfire = -1;
                        for (int ddx = -1; ddx <= 1; ++ddx)
                            for (int ddy = -1; ddy <= 2; ++ddy)
                                for (int ddz = -1; ddz <= 1; ++ddz)
                                    fire_near += neth_vec_nether_block(S.v, i, fx + ddx, fy + ddy, fz + ddz) == 51;
                        if (nt.hood > 0)
                        {
                            static const int HF[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                            int hx = nt.hood_at[0], hy = nt.hood_at[1], hz = nt.hood_at[2];
                            hfire = neth_vec_nether_block(S.v, i, hx, hy + 2, hz) == 51;
                            for (int q = 0; q < 4; ++q)
                            {
                                hfire |= (neth_vec_nether_block(S.v, i, hx + HF[q][0], hy + 1, hz + HF[q][1]) == 51) << (1 + q);
                                hfire |= (neth_vec_nether_block(S.v, i, hx + HF[q][0], hy, hz + HF[q][1]) == 51) << (5 + q);
                            }
                        }
                        /* lane/rods2 (PRIVILEGED, books only): the server's stat.damageDealt and stat.damageTaken
                         * (tenths), the combat tracker's last entry (type, source, damage, ticks since it landed),
                         * the burning bit */
                        int32_t dm_dealt, dm_taken;
                        struct neth_rl_combat_now cbn;
                        neth_rl_damage_stats(S.h, i, &dm_dealt, &dm_taken);
                        neth_rl_combat(S.h, i, &cbn);
                        /* lane/rods2 (PRIVILEGED, books only): the blazes within NETH_RL_FB_R and those with a line to the
                         * eye; the small fireballs new since the last step (fired: the shooter's id is not kept) and
                         * of them the ones with a block between their place and the eye, and all within NETH_RL_FB_R */
                        struct netherview_fireball fbs[NETH_RL_FB_MAX];
                        int fb_blz, fb_los, fb_new = 0, fb_blk = 0, fb_cone, fb_mouth, fb_cell[3];
                        float fb_ld, fb_ldy;
                        double fey = w.p.y + 1.62;
                        int fbn = neth_vec_nether_fire(S.v, i, w.p.x, fey, w.p.z, w.p.yaw, w.p.pitch, &fb_blz, &fb_los,
                                                       &fb_ld, &fb_ldy, &fb_cone, &fb_mouth, fb_cell, fbs);
                        if (fbn < 0) fbn = 0;
                        for (int q = 0; q < fbn; ++q)
                        {
                            int old = 0;
                            for (int u = 0; u < e->fb_n && !old; ++u) old = e->fb_id[u] == fbs[q].id;
                            if (old) continue;
                            fb_new += 1;
                            fb_blk += neth_vec_nether_clear(S.v, i, fbs[q].x, fbs[q].y, fbs[q].z, w.p.x, fey, w.p.z) == 0;
                        }
                        for (int q = 0; q < fbn; ++q) e->fb_id[q] = fbs[q].id;
                        e->fb_n = fbn;
                        fprintf(ntrace_out,
                                "%lld\t%d\t%.3f\t%.3f\t%.3f\t%.1f\t%.1f\t%.1f\t%u\t%d\t%d\t%d\t%d,%d,%d,%d\t%d,%d,%d,%d\t"
                                "%d,%d,%d,%d,%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d,%d,%d\t%d\t%d\t%d\t%d\t"
                                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.2f\t%d\t%lld\t%.2f\t%.2f\t%.2f\t%d\t%d\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.2f\t%.2f\t%d\t%d\t%d\t%lld\t%d\t%d,%d,%d\t%lld\t%lld\t%d,%d,%d,%d\t%d\t%d\n",
                                (long long)e->n, i, w.p.x, w.p.y, w.p.z, w.p.yaw, w.p.pitch, w.p.health,
                                b.label ? (uint32_t)b.label[i].mbit : 0u, w.p.kind, nt.phase, nt.mode, nt.fort[0],
                                nt.fort[1], nt.fort[2], nt.fort[3], nt.sp[0], nt.sp[1], nt.sp[2], nt.sp[3], nt.goal[0],
                                nt.goal[1], nt.goal[2], nt.goal[3], nt.goal[4], nt.cobble,
                                neth_vec_nether_block(S.v, i, fx, fy - 1, fz), neth_vec_nether_block(S.v, i, fx, fy, fz),
                                neth_vec_nether_block(S.v, i, fx, fy + 1, fz), neth_vec_nether_block(S.v, i, ax, fy - 1, az),
                                neth_vec_nether_block(S.v, i, ax, fy, az), w.p.flags, (long long)nt.dig_t, nt.dig[0],
                                nt.dig[1], nt.dig[2], nt.dig_b, nt.dig_lava, nt.dig_seen, nt.refills, nt.camp_age,
                                nt.blz16, nt.blzsp, nt.sp[0] ? neth_vec_nether_block(S.v, i, nt.sp[1], nt.sp[2], nt.sp[3]) : -1,
                                neth_vec_nether_block(S.v, i, fx, fy + 2, fz), neth_vec_nether_block(S.v, i, fx, fy + 3, fz),
                                nt.kills, nt.rods, nt.blz_los, nt.blz_up, nt.blz_dy10, nt.blz_hd10, w.p.food,
                                food_points(&w.p.st), nt.fid, nt.fkind, nt.doing, nt.hood, nt.rc, nt.hold, nt.press,
                                nt.bseen_n, nt.bseen_d, nt.fstuck, (long long)nt.drive_t, nt.pick_at[0], nt.pick_at[1],
                                nt.pick_at[2], nt.pick_age, rg_n, rg_d, rg_dy, hmask, hin, nt.hood_k, nt.bhits,
                                /* lane/rods: the crosshair (its living's id and kind, the block hit or -1) */
                                w.p.st.mo_entity, w.p.st.mo_ekind, w.p.st.mo_hit ? w.p.st.mo_block : -1,
                                /* lane/rods: the lock's blaze (distance, height over the eye, a line; -1 none) */
                                nt.lk_d, nt.lk_dy, nt.lk_los, dm_dealt, dm_taken, cbn.dt, cbn.name, cbn.damage,
                                cbn.age, w.p.fire, fb_blz, fb_los, fbn, fb_new, fb_blk, fb_ld, fb_ldy, nt.cov_here,
                                nt.cov_off, fb_cone, (long long)nt.eye_strikes, fb_mouth,
                                fb_cell[0], fb_cell[1], fb_cell[2], (long long)nt.mouth_steps,
                                (long long)nt.mouths,
                                /* lane/rods2: each armour slot's damage (boots, leggings, chestplate, helmet; -1 none) */
                                w.p.st.armor[0].item >= 0 ? w.p.st.armor[0].damage : -1,
                                w.p.st.armor[1].item >= 0 ? w.p.st.armor[1].damage : -1,
                                w.p.st.armor[2].item >= 0 ? w.p.st.armor[2].damage : -1,
                                w.p.st.armor[3].item >= 0 ? w.p.st.armor[3].damage : -1, fire_near, hfire);
                    }
                    struct neth_vec_sh_trace sq;
                    if (shtrace_out && (shtrace_i < 0 || i == shtrace_i) && w.p.dim == 0 && e->n % shtrace_k == 0 &&
                        neth_vec_sh_trace(S.v, i, &sq) == 0)
                        /* --shold-trace (lane/proom): the step, place, look, health and food, owner, kind, screen, the
                         * day's time, the eyes and snowballs, the latch and each of its holds, the oracle's room */
                        fprintf(shtrace_out,
                                "%lld\t%d\t%.3f\t%.3f\t%.3f\t%.1f\t%.1f\t%.1f\t%d\t%u\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t"
                                "%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t"
                                "%d,%d,%d,%d\t%d\t%d\t%d\t%d\t%d,%d,%d\t%d,%d,%d,%d\t%d,%d,%d,%d,%d,%d\t%d\t%#x\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d,%d,%d\t%d\t%d,%d,%d\t%d\t%lld\t%#x\t%d:%.1f,%.1f\t"
                                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\n",
                                (long long)e->n, i, w.p.x, w.p.y, w.p.z, w.p.yaw, w.p.pitch, w.p.health, w.p.food,
                                b.label ? (uint32_t)b.label[i].mbit : 0u, w.p.kind, sq.screen, (long long)sq.wtime,
                                sq.eyes, sq.snowballs, sq.go, sq.phase, sq.osub, sq.on, sq.want, sq.fwait, sq.sn_hold,
                                (long long)sq.sn_w0, sq.sn_quit, sq.food_pts, sq.food_wait, sq.armor, sq.armor_wait,
                                sq.swords, sq.sword_need, sq.sword_wait, sq.room_ok, sq.room_d, sq.room, sq.lit, sq.filled,
                                sq.gave_up, sq.book_phase, w.p.flags, sq.stand[0], sq.stand[1], sq.stand[2],
                                sq.stand[3], sq.nop, sq.opk, sq.ofail, sq.ooff, sq.dig[0], sq.dig[1], sq.dig[2], sq.sp[0],
                                sq.sp[1], sq.sp[2], sq.sp[3], sq.box[0], sq.box[1], sq.box[2], sq.box[3], sq.box[4],
                                sq.box[5], sq.prov, (unsigned)sq.fs_home, sq.sky, sq.pearl_on, sq.pearl_phase,
                                sq.pearls, sq.hostiles, sq.nframe, sq.frame_d, sq.rstart, sq.wood[0], sq.wood[1],
                                sq.wood[2], sq.wood_blk, sq.wood_stand[0], sq.wood_stand[1], sq.wood_stand[2],
                                sq.wood_logs, (long long)sq.wood_age, (unsigned)b.acts[i].hold, b.acts[i].look_mode,
                                b.acts[i].look[0], b.acts[i].look[1], b.acts[i].press[PK_ATTACK], sq.mo_entity,
                                sq.mo_ekind, sq.lq_hit, sq.lq_block, sq.ptid, sq.pangry, sq.ptdist, sq.phits);
                    if (etrace_out && (etrace_i < 0 || i == etrace_i) && w.p.dim == 1)
                    {
                        double ti[NETH_VEC_DRAGON_INFO];
                        /* the client world's block under the feet and the one under the next cell toward the island's
                         * centre (the larger of |x|, |z| stepped one toward 0) */
                        int fx = (int)floor(w.p.x), fy = (int)floor(w.p.y + 0.01), fz = (int)floor(w.p.z);
                        int ax = fx, az = fz;
                        if (fabs(w.p.x) >= fabs(w.p.z)) ax += w.p.x > 0 ? -1 : 1;
                        else az += w.p.z > 0 ? -1 : 1;
                        if (neth_vec_dragon_info(S.v, i, ti) == 0)
                        {
                            fprintf(etrace_out, "%lld\t%.3f\t%.3f\t%.3f\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.0f\t%d\t%d\t%d\t%d\t%d",
                                    (long long)e->n, w.p.x, w.p.y, w.p.z, w.p.yaw, w.p.pitch, w.p.health, ti[7], ti[9],
                                    ti[37], w.p.kind, w.p.flags, i, neth_vec_nether_block(S.v, i, fx, fy - 1, fz),
                                    neth_vec_nether_block(S.v, i, ax, fy - 1, az));
                            double hu[NETH_VEC_DRAGON_HUNT];
                            if (etrace_hunt && neth_vec_dragon_hunt(S.v, i, hu) == 0)
                                fprintf(etrace_out, "\t%.0f\t%.0f\t%.2f\t%.2f\t%.2f\t%.0f\t%.0f\t%.0f\t%.2f\t%.2f\t%.2f"
                                        "\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%d",
                                        hu[1], hu[2], hu[3], hu[4], hu[5], hu[6], hu[7], hu[8], hu[9], hu[10], hu[11],
                                        hu[12], hu[13], hu[14], hu[15], hu[16], hu[17], hu[18], hu[19],
                                        inv_n(&w.p.st, IT_SNOWBALL));
                            if (etrace_hunt)   /* the hotbar's nine item ids (0 empty) */
                                for (int j = 0; j < 9; ++j)
                                    fprintf(etrace_out, "%c%d", j ? ',' : '\t', w.p.st.inv[j].count > 0 ? w.p.st.inv[j].item : 0);
                            if (etrace_hunt)   /* the blocks a tower takes, held (cobblestone and end stone) */
                                fprintf(etrace_out, "\t%d", inv_n(&w.p.st, IT_COBBLE) + inv_n(&w.p.st, 121));
                            if (etrace_ender)
                            {
                                /* lane/dleg (--end-trace-ender 1, PRIVILEGED, read only): the census's endermen (the
                                 * server's, nearest NETH_RL_CENSUS within NETH_RL_CENSUS_R): alive, listed, the nearest's
                                 * distance (eye to box middle), the screaming ones (the client copy's DataWatcher 18) and
                                 * the nearest of them, and those in the stare's cone with the line clear
                                 * (EntityEnderman.shouldAttackPlayer: the look's dot with the line to the box middle over
                                 * 1 - 0.025 / d), the nearest of them; -1 none (census off: every column -1) */
                                const struct neth_rl_far *ef = neth_rl_far(S.h, i);
                                int en_n = -1, en_alive = -1, en_scr = 0, en_cone = 0;
                                double en_d = -1, en_sd = -1, en_cd = -1;
                                if (ef && ef->census.on)
                                {
                                    const struct neth_rl_census *c = &ef->census;
                                    double yr = w.p.yaw * M_PI / 180.0, pr = w.p.pitch * M_PI / 180.0;
                                    double lx = -sin(yr) * cos(pr), ly = -sin(pr), lz = cos(yr) * cos(pr);
                                    en_alive = c->alive, en_n = c->n < NETH_RL_CENSUS ? c->n : NETH_RL_CENSUS;
                                    for (int k = 0; k < en_n; ++k)
                                    {
                                        double dx = c->e[k].dx, dy = c->e[k].dy, dz = c->e[k].dz;
                                        double dd = sqrt(dx * dx + dy * dy + dz * dz);
                                        if (dd < 1e-6) continue;
                                        if (en_d < 0 || dd < en_d) en_d = dd;
                                        if (c->e[k].angry && ++en_scr && (en_sd < 0 || dd < en_sd)) en_sd = dd;
                                        if (c->e[k].clear && (dx * lx + dy * ly + dz * lz) / dd > 1.0 - 0.025 / dd &&
                                            ++en_cone && (en_cd < 0 || dd < en_cd))
                                            en_cd = dd;
                                    }
                                }
                                fprintf(etrace_out, "\t%d\t%d\t%.1f\t%d\t%.1f\t%d\t%.1f", en_alive, en_n, en_d, en_scr,
                                        en_sd, en_cone, en_cd);
                                /* ... and the server's dragon: its ticks (they stop while its chunks are not all
                                 * loaded) and health, -1 none */
                                const struct neth_rl_end *ev = neth_vec_end_view(S.v, i);
                                fprintf(etrace_out, "\t%d\t%.1f\t%.1f\t%.1f\t%.1f", ev && ev->on ? ev->sv_ticks : -1,
                                        ev && ev->on ? ev->sv_dragon_hp : -1.0f, ev ? ev->sv_pos[0] : 0.0f,
                                        ev ? ev->sv_pos[1] : 0.0f, ev ? ev->sv_pos[2] : 0.0f);
                                /* ... and on an event row (health fell, a wing push, or the feet 0.3 over the last
                                 * row's with a floor under them then) the feet's Chebyshev distance to the nearest
                                 * column with no block from the feet's level down 60 (the client world's; within 10, 11
                                 * none; -1 a column not loaded), else -2 */
                                int under = neth_vec_nether_block(S.v, i, fx, fy - 1, fz), vdist = -2;
                                if (e->vd_ok && (w.p.health < e->vd_hp - 0.01f || (int)ti[37] > e->vd_push ||
                                                 (w.p.y > e->vd_y + 0.3 && e->vd_under > 0)))
                                {
                                    vdist = 11;
                                    for (int rr = 0; rr <= 10 && vdist == 11; ++rr)
                                        for (int cx = fx - rr; cx <= fx + rr && vdist == 11; ++cx)
                                            for (int cz = fz - rr; cz <= fz + rr && vdist == 11; ++cz)
                                            {
                                                if (abs(cx - fx) != rr && abs(cz - fz) != rr) continue;
                                                int any = 0, unk = 0;
                                                for (int yy = fy; yy >= 0 && yy >= fy - 60 && !any; --yy)
                                                {
                                                    int b = neth_vec_nether_block(S.v, i, cx, yy, cz);
                                                    if (b < 0) unk = 1, any = 1;
                                                    else if (b > 0) any = 1;
                                                }
                                                if (unk) vdist = -1;
                                                else if (!any) vdist = rr;
                                            }
                                }
                                fprintf(etrace_out, "\t%d", vdist);
                                /* ... the End books' running damage counters (sword swings at the dragon, the health
                                 * they took, the swings that took any; arrows at it and the health they took; the
                                 * dragon's bites; its healing seen on the boss bar) and the roof: the lowest block
                                 * 1-3 over the head (1 the cell right above it, 0 none; the client world's) */
                                int roof = 0;
                                for (int k = 1; k <= 3 && !roof; ++k)
                                    if (neth_vec_nether_block(S.v, i, fx, fy + 1 + k, fz) > 0) roof = k;
                                fprintf(etrace_out, "\t%.0f\t%.1f\t%.0f\t%.0f\t%.1f\t%.0f\t%.0f\t%d", ti[20], ti[52], ti[53],
                                        ti[22], ti[51], ti[49], ti[76], roof);
                                /* ... the server's crystals standing in its list (sv_ncry) and the dragon's healing
                                 * crystal (1 one, its place; endview's books) */
                                {
                                    const struct neth_rl_end *lv = neth_vec_end_view(S.v, i);
                                    int on = lv && lv->on;
                                    fprintf(etrace_out, "\t%d\t%d\t%.1f\t%.1f\t%.1f", on ? lv->sv_ncry : -1,
                                            on ? lv->sv_heal : -1, on ? lv->sv_heal_at[0] : 0.0f,
                                            on ? lv->sv_heal_at[1] : 0.0f, on ? lv->sv_heal_at[2] : 0.0f);
                                }
                                e->vd_ok = 1, e->vd_hp = w.p.health, e->vd_push = (int)ti[37], e->vd_y = w.p.y;
                                e->vd_under = under;
                            }
                            fputc('\n', etrace_out);
                        }
                    }
                    const char *result = w.p.flags & (PF_DIED | PF_DEAD) ? "death" :
                                         (target_gain && gain_kind != TG_PEARL ? e->tg0 >= 0 && e->tg_max - e->tg0 >= target_gain :
                                          target_gain ? e->hunt_max - e->hunt0 >= target_gain :
                                          target_stage >= 0 ? stage_hit(&w, target_stage, stage_base) : rung >= target)
                                             ? "reached" : e->n >= S.seg_len ? "limit" : NULL;
                    if (result && wood_out && e->wl && e->wl->init && !e->rollout_done)
                    {
                        const struct wl_acc *a = e->wl;
                        const struct neth_rl_state *st = &w.p.st;
                        int fr = 0;
                        for (int j = 0; j < 36; ++j) fr += st->inv[j].item <= 0 || st->inv[j].count <= 0;
                        fprintf(wood_out, "E\t%d\t%s\t%lld\t%lld\t%.1f\t%lld\t%lld\t%lld\t%lld\t%.1f\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%d\t%d",
                                i, result, (long long)e->n, (long long)a->seen17, (double)a->seen_d, (long long)a->aim,
                                (long long)a->mined, (long long)a->held, (long long)a->wpick, a->maxd, a->ymin, a->ymax,
                                (long long)a->water, (long long)a->full, (long long)a->roofed, fr, st->mined_logs);
                        fputc('\t', wood_out);
                        for (int k = 0; k < 6; ++k) fprintf(wood_out, "%s%lld", k ? "," : "", (long long)a->own[k]);
                        fputc('\t', wood_out);
                        for (int k = 0; k < 6; ++k) fprintf(wood_out, "%s%lld", k ? "," : "", (long long)a->surf[k]);
                        int64_t wb[4] = {0, 0, 0, 0};
                        neth_vec_wood(S.v, i, wb);
                        fprintf(wood_out, "\t%lld\t%lld\t%lld\t%lld,%lld,%lld,%lld\n", (long long)a->rescue_runs,
                                (long long)a->rescue_long, (long long)a->rescue_max, (long long)wb[0],
                                (long long)wb[1], (long long)wb[2], (long long)wb[3]);
                        fflush(wood_out);
                    }
                    if (result && stage_out && !e->rollout_done)
                    {
                        /* lane/fwd: the END row (the state at the last step, the restore check's terms) and
                         * --crossings' steps */
                        fprintf(stage_out, "%d\tEND:%s\t%lld\t%lld\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%d\t%d\t%d\t%u\t", i, result,
                                (long long)e->n, (long long)w.p.world_time, w.p.dim, w.p.x, w.p.y, w.p.z, w.p.health,
                                w.p.food, food_points(&w.p.st), w.p.kind, b.label ? (uint32_t)b.label[i].mbit : 0u);
                        int any = 0;
                        for (int j = 0; j < 36; ++j)
                            if (w.p.st.inv[j].item > 0 && w.p.st.inv[j].count > 0)
                                fprintf(stage_out, "%s%d:%d", any++ ? "," : "", w.p.st.inv[j].item, w.p.st.inv[j].count);
                        fprintf(stage_out, "%s\n", any ? "" : "-");
                        fflush(stage_out);
                        if (cross_dir)
                        {
                            char sp[2400];
                            snprintf(sp, sizeof sp, "%s/seg/1.bin", cross_dir);
                            if (write_seg_path(sp, 1, 0, e, &w.p))
                                printf("crossings: %s not written\n", sp);
                        }
                    }
                    if (result)
                    {
                        int egg = -1, fire = 0, air = 0, food0 = 0, fall = 0;
                        if (result[0] == 'd')
                        {
                            struct neth_vec_db db;
                            if (neth_vec_db(S.v, i, &db) == 0 && db.ndeath > 0)
                            {
                                int j = db.ndeath - 1;
                                if (j >= NETH_VEC_DB_DEATHS) j = NETH_VEC_DB_DEATHS - 1;
                                egg = db.d_egg[j], fire = db.d_fire[j], air = db.d_air[j];
                                food0 = db.d_food[j], fall = db.d_fall[j];
                            }
                        }
                        char from[64], to[64], last[64];
                        rung_name(S.nd[rollout_root].rung, from, sizeof from);
                        if (target_gain) snprintf(to, sizeof to, "%s+%d", gain_kind == TG_ROD ? "rods" : gain_kind == TG_OBS ? "obsidian" :
                                                  gain_kind == TG_ARMOR ? "armor" : "pearls",
                                              target_gain);
                        else if (target_stage >= 0) snprintf(to, sizeof to, "%s", STAGE_CHAIN[target_stage].name);
                        else rung_name(target, to, sizeof to);
                        if (gain_out)
                        {
                            /* --gain-log (lane/rods): the counts, each gain as step:owner bit:health, the end's state */
                            struct neth_vec_nether nt = {0};
                            neth_vec_nether(S.v, i, &nt);
                            int n0 = e->tg0 < 0 ? 0 : e->tg0, ng = e->tg_max - n0;
                            if (ng > CH_GAIN_MAX) ng = CH_GAIN_MAX;
                            fprintf(gain_out, "%d\t%s\t%lld\t%d\t%d\t%d\t", i, result, (long long)e->n, n0,
                                    e->tg0 < 0 ? 0 : e->tg_max, gain_count(&w.p.st, gain_kind));
                            for (int k = 0; k < ng; ++k)
                                fprintf(gain_out, "%s%lld:%u:%.1f", k ? "," : "", (long long)e->tg_step[k], e->tg_mb[k],
                                        e->tg_hp[k]);
                            fprintf(gain_out, "%s\t%.1f\t%d\t%d\t%d\t%u\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t",
                                    ng ? "" : "-", w.p.health, w.p.food, food_points(&w.p.st), w.p.kind,
                                    b.label ? (uint32_t)b.label[i].mbit : 0u, w.p.x, w.p.y, w.p.z, nt.phase, nt.mode,
                                    nt.kills, nt.bkills, nt.bstat, nt.camp_age, nt.cobble);
                            for (int k = 0; k < 6; ++k) fprintf(gain_out, "%s%d", k ? "," : "", nt.cause[k]);
                            fputc('\n', gain_out);
                            fflush(gain_out);
                        }
                        if (supply_out)
                        {
                            /* --supply-log (lane/supply, books only): the end's holdings */
                            struct neth_vec_kit kit = {0};
                            neth_vec_kit(S.v, i, &kit);
                            char buf[SP_ROW];
                            supply_fmt(buf, e->n, &w.p, &kit, e->sp_arm, e->sp_rung);
                            supply_row(supply_out, i, result, "end", buf);
                            if (!strcmp(result, "death") && e->sp_alive)
                                supply_row(supply_out, i, result, "alive", e->sp_alive);
                        }
                        if (choice_out)
                        {
                            /* --choice-log (lane/choice, books only): the attempt's totals at its end */
                            struct neth_vec_kit kit = {0};
                            neth_vec_kit(S.v, i, &kit);
                            fprintf(choice_out, "E\t%d\t%s\t%lld\t-\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t-\t-\t-\t-\t-\t"
                                    "-\t%d\t%d\t%d\t%d\t%d\t%.2f\t%d\n", i, result, (long long)e->n,
                                    (long long)kit.ch_queries, (long long)kit.ch_conflicts, (long long)kit.ch_armor,
                                    (long long)kit.ch_ipick, (long long)kit.ch_invalid, (long long)kit.ch_check,
                                    (long long)kit.ch_check_diff, inv_n(&w.p.st, 257), inv_n(&w.p.st, 278),
                                    inv_n(&w.p.st, 265), armor_pts(&w.p.st), w.p.dim, w.p.health, w.p.food);
                            fflush(choice_out);
                        }
                        if (build_out)
                        {
                            /* --build-log (lane/bportal): the build's steps' first steps, the bucket portal's and the
                             * obsidian macro's books, the end's place, the obsidian books' steps by who (OW_*) */
                            struct neth_vec_bportal bb;
                            struct neth_vec_obsidian ob;
                            if (neth_vec_bportal(S.v, i, &bb) != 0) memset(&bb, 0, sizeof bb);
                            if (neth_vec_obsidian(S.v, i, &ob) != 0) memset(&ob, 0, sizeof ob);
                            fprintf(build_out, "%d\t%s\t%lld\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t"
                                               "%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%.2f\t%.2f\t%.2f\t",
                                    i, result, (long long)e->n, e->bl_obs0, e->bl_obsmax, inv_n(&w.p.st, IT_OBSIDIAN),
                                    e->bl_keep, (long long)e->bl_first, (long long)e->bl_10, (long long)e->bl_kept,
                                    (long long)e->bl_plan, (long long)e->bl_frame, (long long)e->bl_lit,
                                    (long long)e->bl_nether, bb.starts, (long long)bb.steps_on, bb.sites, bb.casts,
                                    bb.best_casts, bb.lit, ob.mined, w.p.dim, w.p.x, w.p.y, w.p.z);
                            for (int k = 0; k < 16; ++k) fprintf(build_out, "%s%d", k ? "," : "", ob.steps[k]);
                            fprintf(build_out, "\t%d\t%d\t%d\t%d\t%d", e->bl_fp0, e->bl_fpk, e->bl_fpf, e->bl_fpn,
                                    e->bl_hn);
                            /* the food census over the attempt and over the wait from bl_kept (FOOD_BOOK_COLS) */
                            struct neth_vec_food fo, z;
                            memset(&z, 0, sizeof z);
                            if (neth_vec_food(S.v, i, &fo) != 0) memset(&fo, 0, sizeof fo);
                            for (int part = 0; part < 2; ++part)
                            {
                                const struct neth_vec_food *a = part ? (e->bl_fok ? &e->bl_fo : NULL) : &z;
                                if (!a)
                                {
                                    fputs("\t-", build_out);
                                    continue;
                                }
                                int32_t q[] = {fo.hunts - a->hunts, fo.hunt_end[0] - a->hunt_end[0],
                                               fo.hunt_end[1] - a->hunt_end[1], fo.hunt_end[2] - a->hunt_end[2],
                                               fo.hunt_end[3] - a->hunt_end[3], fo.lost_why[0] - a->lost_why[0],
                                               fo.lost_why[1] - a->lost_why[1], fo.lost_why[2] - a->lost_why[2],
                                               fo.lost_why[3] - a->lost_why[3], fo.lost_why[4] - a->lost_why[4],
                                               fo.scouts - a->scouts, fo.scout_found - a->scout_found,
                                               fo.scout_end[0] - a->scout_end[0], fo.scout_end[1] - a->scout_end[1],
                                               fo.scout_end[2] - a->scout_end[2], fo.scout_end[3] - a->scout_end[3],
                                               fo.scout_end[4] - a->scout_end[4], fo.scout_end[5] - a->scout_end[5],
                                               fo.meat_got - a->meat_got, fo.hunt_meat - a->hunt_meat,
                                               fo.other_got - a->other_got, fo.eats - a->eats,
                                               fo.trips - a->trips, fo.trip_meat - a->trip_meat,
                                               fo.animal_steps - a->animal_steps};
                                for (size_t k = 0; k < sizeof q / sizeof *q; ++k)
                                    fprintf(build_out, "%c%d", k ? ',' : '\t', q[k]);
                            }
                            fputc('\n', build_out);
                            fflush(build_out);
                        }
                        rung_name(e->best, last, sizeof last);
                        fprintf(rollout_out, "%d\t%llu\t%d\t%s\t%s\t%s\t%lld\t%s\t%u\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\n",
                                i, (unsigned long long)(seed + (uint64_t)i), E[0].rfrom, from, to, result,
                                (long long)e->n, last, b.label ? b.label[i].mbit : 0, w.p.kind,
                                w.p.health, w.p.food, egg, fire, air, food0, fall);
                        fflush(rollout_out);
                        if (ladder_out)
                        {
                            fprintf(ladder_out, "%d\t%s\t%lld", i, result, (long long)e->n);
                            for (int k = 0; k < LAD_RUNGS; ++k) fprintf(ladder_out, "\t%lld", (long long)e->ld_at[k]);
                            for (int k = 0; k < 11; ++k)
                                fprintf(ladder_out, k < 6 || k == 7 ? "\t%.0f" : "\t%.1f", e->ld_ok ? e->ld0[k] : -1.0f);
                            fprintf(ladder_out, "\t%d\t%d\t%.1f\t%d\t%.1f\t%.0f\t%.1f\t%.1f\t%.1f\t%d\t%d\t%lld\t%d",
                                    inv_n(&w.p.st, IT_SNOWBALL), inv_n(&w.p.st, 262), w.p.health,
                                    food_points(&w.p.st), e->ld_ok ? e->ld_hpmin : -1.0f,
                                    e->ld_dmin < 1e8f ? e->ld_dmin : -1.0f, w.p.x, w.p.y, w.p.z,
                                    e->lc_n, e->lc_ngone, (long long)e->lc_last, e->lc_g75);
                            /* ... then the server's standing crystals at the end (its list: x,y,z;..., - none; ? no
                             * list), the books' */
                            {
                                const struct neth_rl_end *lv = w.p.dim == 1 ? neth_vec_end_view(S.v, i) : NULL;
                                int nc = lv && lv->on ? lv->sv_ncry : -1;
                                if (nc < 0) fputs("\t?", ladder_out);
                                else if (nc == 0) fputs("\t-", ladder_out);
                                for (int c = 0; c < nc && c < NETH_RL_END_CRYSTALS; ++c)
                                    fprintf(ladder_out, "%s%.1f,%.1f,%.1f", c ? ";" : "\t", lv->sv_cry[c][0],
                                            lv->sv_cry[c][1], lv->sv_cry[c][2]);
                                fputc('\n', ladder_out);
                            }
                            fflush(ladder_out);
                        }
                        if (hunt_out)
                        {
                            struct neth_vec_pearl pb;
                            struct neth_vec_sh_trace hq;
                            if (neth_vec_pearl(S.v, i, 1, &pb) != 0) memset(&pb, 0, sizeof pb), pb.death_step = -1;
                            int sky = neth_vec_sh_trace(S.v, i, &hq) == 0 ? hq.sky : -1;
                            int n0 = e->hunt0 < 0 ? 0 : e->hunt0;
                            fprintf(hunt_out, "%d\t%s\t%lld\t%d\t%d\t%d\t", i, result, (long long)e->n, n0, e->hunt_max,
                                    inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE));
                            int ng = e->hunt_max - n0;
                            if (ng > 12) ng = 12;
                            for (int k = 0; k < ng; ++k)   /* step:day time:owner bit, a gain each */
                                fprintf(hunt_out, "%s%lld:%lld:%u", k ? "," : "", (long long)e->gain_step[k],
                                        (long long)e->gain_wt[k], e->gain_mb[k]);
                            fprintf(hunt_out, "%s\t%lld\t%u\t%d\t%.1f\t%d\t%d\t%d\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t",
                                    ng ? "" : "-", (long long)(w.p.world_time % 24000),
                                    b.label ? (uint32_t)b.label[i].mbit : 0u, w.p.kind, w.p.health, w.p.food,
                                    food_points(&w.p.st), w.armor, w.p.y, sky, w.p.st.nvis,
                                    pb.death_step >= 0 ? pb.cause[0] : -1, pb.death_step >= 0 ? pb.death_mbit : -1,
                                    pb.death_step >= 0 ? pb.death_kind : -1, pb.death_step >= 0 ? pb.death_phase : -1,
                                    pb.death_step >= 0 ? pb.death_y : 0.0f, pb.death_step >= 0 ? pb.death_wet : -1,
                                    pb.seen, pb.provoked, pb.killed);
                            for (int k = 0; k < 12; ++k) fprintf(hunt_out, "%s%d", k ? "," : "", pb.why[k]);
                            fputc('\t', hunt_out);
                            for (int k = 0; k < 12; ++k) fprintf(hunt_out, "%s%d", k ? "," : "", pb.phase_steps[k]);
                            fprintf(hunt_out, "\t%d\t%d\t%d\t%d\t%d\t", pb.hits, pb.drops, pb.fetched, pb.fetch_out,
                                    pb.item_steps);
                            for (int k = 0; k < 5; ++k) fprintf(hunt_out, "%s%d", k ? "," : "", pb.prov_end[k]);
                            const struct neth_rl_far *cf = neth_rl_far(S.h, i);
                            int32_t aim[3] = {-1, -1, -1};
                            if (neth_vec_pearl_aim(S.v, i, aim) != 0) aim[0] = aim[1] = aim[2] = -1;
                            fprintf(hunt_out, "\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\t%d\t%d\t%d\t%d\t", (long long)e->hunt_night,
                                    (long long)e->ender_view, (long long)e->night_steps, (long long)e->alive64,
                                    (long long)e->alive128, cf && e->ek0 >= 0 ? cf->ender_kills - e->ek0 : -1, pb.it_new,
                                    aim[0], aim[1], aim[2]);
                            int32_t pt[8] = {0};
                            if (neth_vec_pearl_patrol(S.v, i, pt) != 0) pt[0] = -1;
                            for (int k = 0; k < 8; ++k) fprintf(hunt_out, "%s%d", k ? "," : "", pt[k]);
                            fputc('\n', hunt_out);
                            fflush(hunt_out);
                        }
                        if (nlog_out && e->nl)
                        {
                            const struct neth_rl_far *nf = neth_rl_far(S.h, i);
                            nl_row(nlog_out, i, e, result, &w, nf ? nf->ender_kills : -1,
                                   inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE), w.p.st.mob_kills);
                        }
                        if (flog_out && e->fl)
                        {
                            const struct neth_rl_far *nf = neth_rl_far(S.h, i);
                            fl_row(flog_out, i, e->fl, result, inv_n(&w.p.st, IT_PEARL) + inv_n(&w.p.st, IT_EYE),
                                   nf ? nf->ender_kills : -1);
                            fl_crow(flog_out, i, e->fl);
                            fflush(flog_out);
                            free(e->fl), e->fl = NULL;
                        }
                        if (slog_out && e->sg)
                        {
                            for (int si = 0; si < e->sg->n; ++si) sg_row(slog_out, i, &e->sg->e[si], "end");
                            e->sg->n = 0;
                            fflush(slog_out);
                        }
                        if (dlog_out)
                        {
                            struct dg_acc z0, *g = e->dg ? e->dg : &z0;
                            if (!e->dg) memset(&z0, 0, sizeof z0);
                            for (int vi = 0; vi < g->nv; ++vi) dg_vrow(dlog_out, i, &g->v[vi], "end");
                            g->nv = 0;
                            if (e->dg && g->day0 >= 0) dg_nrow(dlog_out, i, g, result);
                            struct neth_vec_strip sb;
                            if (neth_vec_strip(S.v, i, &sb) != 0) memset(&sb, 0, sizeof sb);
                            fprintf(dlog_out, "A\t%d\t%s\t%lld\t%lld,%lld", i, result, (long long)e->n,
                                    (long long)g->steps[0], (long long)g->steps[1]);
                            for (int bb = 0; bb < 3; ++bb)
                                fprintf(dlog_out, "\t%lld,%lld", (long long)g->es[bb][0], (long long)g->es[bb][1]);
                            fputc('\t', dlog_out);
                            for (int k = 0; k < 4; ++k)
                                fprintf(dlog_out, "%s%lld,%lld", k ? ";" : "", (long long)g->dyb[k][0],
                                        (long long)g->dyb[k][1]);
                            fputc('\t', dlog_out);
                            for (int k = 0; k < 3; ++k)
                                fprintf(dlog_out, "%s%lld,%lld", k ? ";" : "", (long long)g->line[k][0],
                                        (long long)g->line[k][1]);
                            struct neth_vec_pearl pq;
                            if (neth_vec_pearl(S.v, i, 1, &pq) != 0) memset(&pq, 0, sizeof pq);
                            fprintf(dlog_out, "\t%d,%lld,%d\t%d,%d,%d\n", sb.cells, (long long)sb.strip_steps, sb.got[5],
                                    pq.home_digs, pq.oracle_steps, pq.oracle_path);
                            fflush(dlog_out);
                        }
                        if (dtrace_out && result[0] == 'd' && e->dt) dt_dump(dtrace_out, i, e->dt, e->dt_n);
                        if (lvlog_out && result[0] == 'd' && e->lv)
                        {
                            /* lane/lava (--lava-log): the ring, oldest first */
                            int64_t n = e->lv_n < LV_RING ? e->lv_n : LV_RING;
                            for (int64_t k = e->lv_n - n; k < e->lv_n; ++k)
                            {
                                const struct lv_row *q = &e->lv[k % LV_RING];
                                fprintf(lvlog_out,
                                        "%d\t%lld\t%lld\t%u\t%d\t%d\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%x\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t"
                                        "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t"
                                        "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\n",
                                        i, (long long)(e->lv_n - 1 - k), (long long)q->step, q->mbit, q->kind, q->lsrc, q->x,
                                        q->y, q->z, q->yaw, q->pitch, q->hold, q->press_att, q->press_use, q->hotbar, q->fire,
                                        q->health, q->og, q->eye_water, q->held, q->mo_hit, q->mo_dx, q->mo_dy, q->mo_dz,
                                        q->mo_block, q->lq_hit, q->lq_block, q->lq_d, q->hot_n, q->hot_d, q->edge, q->st_hot,
                                        q->st_near, q->ob_hot, q->esc, q->sdir, (long long)q->lava_t, q->b_feet, q->b_head,
                                        q->b_under, q->lv_touch, q->lv_1, q->lv_box, q->lv_dx, q->lv_dy, q->lv_dz,
                                        q->mo_lavanb, q->fire_t, q->water_d);
                            }
                            fflush(lvlog_out);
                        }
                        if (census_out)
                        {
                            /* attempt, result, ticks, then owner:steps for each macro that owned a step (none: no
                             * macro), then kind:steps (vecenv.c M_*: none surface screen loot mine craft, 6 other) */
                            fprintf(census_out, "%d\t%s\t%lld\t", i, result, (long long)e->n);
                            int any = 0;
                            for (int k = 0; k <= 32; ++k)
                            {
                                if (!e->own[k]) continue;
                                const char *nm = "none";
                                if (k < 32)
                                {
                                    nm = "?";
                                    int32_t bit;
                                    for (int q = 0; neth_vec_macro_switch(q, &bit); ++q)
                                        if ((uint32_t)bit == 1u << k) nm = neth_vec_macro_switch(q, NULL);
                                    /* lane/wood: bit 15 as a label is the screen macro (NETH_VEC_LBL_SCREEN), which
                                     * shares NETH_VEC_PICK's bit (mbit never names the pick switch) */
                                    if (k == 15) nm = "screen";
                                }
                                fprintf(census_out, "%s%s:%lld", any++ ? "," : "", nm, (long long)e->own[k]);
                            }
                            fputc('\t', census_out);
                            for (int k = 0; k < 8; ++k) fprintf(census_out, "%s%lld", k ? "," : "", (long long)e->kinds[k]);
                            fputc('\t', census_out);    /* hand_steps (lane/supply): owner:steps, '-' none */
                            any = 0;
                            for (int k = 0; k <= 32; ++k)
                            {
                                if (!e->hand[k]) continue;
                                const char *nm = "none";
                                if (k < 32)
                                {
                                    nm = "?";
                                    int32_t bit;
                                    for (int q = 0; neth_vec_macro_switch(q, &bit); ++q)
                                        if ((uint32_t)bit == 1u << k) nm = neth_vec_macro_switch(q, NULL);
                                    /* lane/wood: bit 15 as a label is the screen macro (NETH_VEC_LBL_SCREEN), which
                                     * shares NETH_VEC_PICK's bit (mbit never names the pick switch) */
                                    if (k == 15) nm = "screen";
                                }
                                fprintf(census_out, "%s%s:%lld", any++ ? "," : "", nm, (long long)e->hand[k]);
                            }
                            if (!any) fputc('-', census_out);
                            fputc('\n', census_out);
                            fflush(census_out);
                        }
                        double di[NETH_VEC_DRAGON_INFO];
                        if (end_out && neth_vec_dragon_info(S.v, i, di) == 0)
                        {
                            fprintf(end_out,
                                    "%d\t%s\t%lld\t%.0f\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.2f\t"
                                    "%.2f\t%.2f\t%.1f\t%.0f\t%.0f\n",
                                    i, result, (long long)e->n, di[0], di[1], di[2], di[3], di[4], di[7], di[9], di[14],
                                    di[24], di[25], di[26], di[27], di[28], di[29], di[30], di[31], di[37], di[15]);
                            fflush(end_out);
                        }
                        e->rollout_done = 1;
                        if (tape_f && i == tape_i)
                        {
                            /* lane/dleg: the attempt's last state at full precision (the health a float to 9 digits,
                             * the place to 17) for chain-ka's exact check against the Java client's last row */
                            printf("tape-attempt end: attempt %d ticks %lld health %.9g food %d x %.17g y %.17g z %.17g\n",
                                   i, (long long)e->n, (double)w.p.health, (int)w.p.food, w.p.x, w.p.y, w.p.z);
                            fflush(stdout);
                            fclose(tape_f), tape_f = NULL;
                        }
                        ++rollout_finished;
                    }
                    else if (chooser)
                        chooser_act(chooser, e->best, b.kind[i], b.actions + (size_t)i * aw, aw, &e->rng, camw, keyp,
                                    keys_off);
                    else env_policy(b.actions + (size_t)i * aw, aw, &e->rng, camw, keyp, keys_off);
                    continue;
                }
                if (trace_out && (trace_env < 0 || i == trace_env) && e->node >= 0 &&
                    (e->n == 1 || e->n % trace_every == 0 || e->n == trace_steps))
                {
                    struct neth_vec_sh_book sh = {0};
                    struct neth_vec_food fo = {0};
                    struct neth_vec_pearl pl = {0};
                    const struct neth_rl_far *far = neth_rl_far(S.h, i);
                    neth_vec_stronghold_book(S.v, i, 0, &sh);
                    neth_vec_food(S.v, i, &fo);
                    neth_vec_pearl(S.v, i, 1, &pl);
                    fprintf(trace_out, "%lld\t%lld\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                            (long long)e->n, (long long)w.p.world_time, w.p.x, w.p.y, w.p.z, w.toward_d,
                            w.p.health, w.p.food, food_points(&w.p.st), inv_n(&w.p.st, IT_EYE), b.kind[i],
                            sh.phase, sh.osub, sh.room, sh.frame, sh.filled, sh.bridges,
                            sh.throws, sh.near, sh.lost, sh.hides, sh.hid, sh.skies,
                            sh.opaths, sh.stuck, prog_of(&w, S.nd[e->node].rung));
                    fprintf(trace_out, "\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%d\t%d",
                            fo.scouts, fo.scout_found, fo.hunts, fo.meat_got,
                            far && far->census.on ? far->census.food_n : -1,
                            far && far->census.on ? far->census.food_x : 0.0f,
                            far && far->census.on ? far->census.food_z : 0.0f,
                            inv_n(&w.p.st, IT_PORK) + inv_n(&w.p.st, IT_BEEF) + inv_n(&w.p.st, IT_CHICKEN),
                            inv_n(&w.p.st, IT_PORK_C) + inv_n(&w.p.st, IT_BEEF_C) + inv_n(&w.p.st, IT_CHICKEN_C));
                    int free_slots = 0;
                    for (int j = 0; j < 36; ++j)
                        free_slots += w.p.st.inv[j].count <= 0;
                    fprintf(trace_out, "\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                            w.p.air, free_slots, pl.phase, pl.on,
                            far && far->census.on ? far->census.pearl_n : -1,
                            far && far->census.on ? far->census.pearl_x : 0.0f,
                            far && far->census.on ? far->census.pearl_y : 0.0f,
                            far && far->census.on ? far->census.pearl_z : 0.0f,
                            pl.item_walk, pl.it_full, pl.it_wet,
                            pl.it_wk[0], pl.it_wk[1], pl.it_wk[2], pl.it_wk[3]);
                    struct neth_vec_chain_controller_probe cp = {0};
                    neth_vec_chain_controller_probe(S.v, i, &cp);
                    fprintf(trace_out, "\t%d\t%d\t%d\t%d\t%u\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                            inv_n(&w.p.st, IT_LOG) + inv_n(&w.p.st, IT_LOG2),
                            inv_n(&w.p.st, IT_PLANKS), inv_n(&w.p.st, IT_COAL), inv_n(&w.p.st, IT_FURNACE),
                            b.label ? b.label[i].mbit : 0,
                            (long long)(cp.stronghold_wait_at > 0 ? cp.steps + 1 - cp.stronghold_wait_at : 0),
                            cp.portal_step, cp.portal_x, cp.portal_y, cp.portal_z,
                            fo.scout_end[1], fo.scout_end[2], fo.scout_end[4], fo.scout_end[5]);
                    struct neth_vec_kit kit = {0};
                    neth_vec_kit(S.v, i, &kit);
                    struct neth_vec_nether nether = {0};
                    neth_vec_nether(S.v, i, &nether);
                    fprintf(trace_out, "\t%u\t%d\t%d\t%d\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                            r->act.hold, r->act.press[PK_ATTACK], r->act.look_mode, r->act.hotbar,
                            r->act.look[0], r->act.look[1], w.p.st.mo_hit, w.p.st.mo_block,
                            w.p.st.mo_x, w.p.st.mo_y, w.p.st.mo_z, w.p.flags,
                            kit.on, kit.n_ore, kit.n_ingot, kit.points, kit.plans);
                    fprintf(trace_out, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%lld\n",
                            nether.phase, nether.fort[0], nether.fort[1], nether.fort[2], nether.fort[3],
                            nether.loc[0], nether.loc[1], nether.loc[2], nether.opaths, nether.opaths_ok,
                            (long long)nether.or_lock_steps, nether.mode, nether.cobble, w.entry_d, w.goal_d,
                            nether.sp[0], nether.sp[1], nether.sp[2], nether.sp[3], (long long)nether.fortin);
                    fflush(trace_out);
                    if (e->n >= trace_steps) done = 1;
                }
                w.carry = !S.carry || S.nd[e->node].carry || rr > R_OBS20 ||
                          (inv_n(&w.p.st, IT_OBSIDIAN) >= S.carry && inv_n(&w.p.st, IT_FLINT_STEEL) > 0);
                /* (the armor: worn by the chain before, or a resumed chain past the armor's rung) */
                /* (the armor worn is the client's own now, a resumed chain's too: no assumption) */
                if (w.p.dim != 1) e->armed = w.armor >= S.armor_min;
                if (w.p.flags & (PF_DIED | PF_DEAD))
                {
                    ++S.deaths;
                    ++S.ddeath[S.nd[e->node].rung];
                    if (death_out && (w.p.flags & PF_DIED))
                    {
                        struct neth_vec_db db;
                        if (neth_vec_db(S.v, i, &db) == 0 && db.ndeath > 0)
                        {
                            int d = db.ndeath - 1;
                            if (d >= NETH_VEC_DB_DEATHS) d = NETH_VEC_DB_DEATHS - 1;
                            fprintf(death_out, "%d\t%d\t%d\t%lld\t%lld\t%d\t%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\n",
                                    i, e->node, S.nd[e->node].rung, (long long)e->n, (long long)w.p.world_time,
                                    db.d_dim[d], w.p.x, w.p.y, w.p.z, db.d_egg[d], db.d_fire[d], db.d_air[d],
                                    db.d_food[d], db.d_fall[d], db.d_mb[d]);
                            fflush(death_out);
                        }
                    }
                    if (S.nd[e->node].rung == R_SNOW && (w.p.flags & PF_DIED))
                    {
                        struct neth_vec_db db;
                        if (neth_vec_db(S.v, i, &db) == 0 && db.ndeath > 0)
                        {
                            int d = db.ndeath - 1;
                            printf("  snow death: env %d from node %d step %lld world_time %lld day_tick %lld "
                                   "dim %d x %.1f y %.1f z %.1f egg %d fire %d air %d food0 %d fall %d "
                                   "macro %d health %.1f food %d\n",
                                   i, e->node, (long long)e->n, (long long)w.p.world_time,
                                   (long long)(w.p.world_time % 24000), db.d_dim[d], w.p.x, w.p.y, w.p.z,
                                   db.d_egg[d], db.d_fire[d], db.d_air[d], db.d_food[d], db.d_fall[d],
                                   db.d_mb[d], w.p.health, w.p.food);
                        }
                    }
                    /* the way home's deaths: where (the step's own position against the spawner the macro remembers
                     * and the arrival portal), burning or not, the health the step began with */
                    struct neth_vec_nether nh;
                    if (S.nd[e->node].rung >= R_ROD7 && S.nd[e->node].rung < R_HOME && w.p.dim == -1 &&
                        neth_vec_nether(S.v, i, &nh) == 0)
                    {
                        S.hm[1] += 1;
                        S.hm[w.p.fire > 0 ? 3 : 5] += 1;
                        double ds = nh.sp[0] ? hypot(w.p.x - nh.sp[1], w.p.z - nh.sp[3]) : -1;
                        double dp = nh.portal[0] ? hypot(w.p.x - nh.portal[1], w.p.z - nh.portal[3]) : -1;
                        S.hm[8 + (ds < 0 ? 4 : ds < 8 ? 0 : ds < 20 ? 1 : ds < 60 ? 2 : 3)] += 1;
                        if (dp >= 0 && dp < 10) S.hm[13] += 1;
                    }
                    if (e->day) S.day_died++;
                    e->ending = 1;
                }
                else
                {
                    if (force_save_at > 0 && e->node > 0 && e->n == force_save_at)
                        e->force_save = 1, e->rrung = S.nd[e->node].rung;
                    int rg = rung_of(&w), pg = prog_of(&w, rg);
                    /* A saved chain keeps its judged rung, except when this run raised the armor floor. A 13-point
                     * pearl node then starts on land until the helmet is worn, and its ore/ingot gains must save. */
                    if (e->force_save && e->rrung > rg &&
                        !(S.armor_min > CH_ARMOR && w.p.dim == 0 && w.armor < S.armor_min))
                        rg = e->rrung, pg = prog_of(&w, rg);
                    int gain = rg > e->best || (rg == e->best && pg >= e->bprog + CH_PROG_STEP) || e->force_save;
                    /* (lane/chain2: no End state saved under CH_END_HP health: s42k2-21 saved states at health 0 and food 7, a
                     * gain made while dying) */
                    /* (and anywhere, a gain inside the rung at that health: s42k3's best on_land state had health 6
                     * and food 0); each cut counted once a segment */
                    if (gain && !e->force_save && S.end_fed && w.endfood > 0 && w.p.dim == 1 && !e->fed) gain = 0;
                    /* (--end-armor: and in iron armor, CH_ARMOR points worn at its last step outside) */
                    if (gain && !e->force_save && S.end_armor && w.p.dim == 1 && !e->armed) gain = 0;
                    if (gain && !e->force_save && w.p.health < CH_END_HP && (w.p.dim == 1 || rg <= e->best))
                    {
                        if (!e->cut) S.floor_cut[w.p.dim == 1 ? 0 : 1] += 1, e->cut = 1;
                        gain = 0;
                    }
                    if (gain && e->gain_at < 0) e->gain_at = e->n;
                    /* (in the End at once: the dragon macro never stops, and 318 of 356 End segments died; 34 had the
                     * dragon at 150 or under and saved nothing, s42k2-18) */
                    if (e->gain_at >= 0 && gain && (w.p.kind == 0 || e->force_save || e->n - e->gain_at >= S.save_wait || w.p.dim == 1))
                    {
                        int st = neth_vec_chain_save(S.v, i, err, sizeof err);
                        /* (refused: every save slot busy, an End save replays its segment twice: lane/chain2 counts them and
                         * keeps the gain pending, which the lowest health seen holds, until a slot frees) */
                        if (st < 0 && S.save_busy++ % 200 == 0)
                            printf("  save refused (%lld so far, env %d, rung %d): %s\n", (long long)S.save_busy, i, rg, err);
                        if (st >= 0)
                        {
                            struct node *pn = &S.nd[e->node];
                            struct node *nd = node_new(&S);
                            pn = &S.nd[e->node];
                            nd->parent = e->node, nd->rung = rg, nd->start = st, nd->state = NS_MAKING;
                            nd->bad = pn->bad; /* an active segment can finish after its parent is rejected */
                            if (e->day) S.day_saved++;
                            nd->q = quality(&w.p);
                            nd->prog = pg;
                            nd->prov = w.prov;
                            nd->meat = w.meat;
                            nd->carry = w.carry;
                            nd->arm = w.armor >= S.armor_min;
                            nd->fed = e->fed;
                            nd->armed = e->armed, nd->armw = w.armor;
                            sword_gear(&w.p.st, &nd->swords, &nd->fresh_swords, &nd->held_sword_wear);
                            nd->cry = w.cry_gone;
                            nd->svc = -1, nd->dhp = (float)w.dragon_hp;
                            const struct neth_rl_end *ev = w.p.dim == 1 ? neth_vec_end_view(S.v, i) : NULL;
                            if (ev)
                            {
                                /* (the books': the server's crystals left, the first 200 End saves a line each) */
                                nd->svc = ev->sv_ncry;
                                static int nsv;
                                if (nsv++ < 200)
                                {
                                    printf("  end save: node %d parent %d prog %d cry %d dragon %.1f armor %d at %.1f %.1f %.1f "
                                           "swords %d fresh %d held_wear %d crystals %d:", nd->id, nd->parent, pg,
                                           w.cry_gone, w.dragon_hp, w.armor, w.p.x, w.p.y, w.p.z, nd->swords,
                                           nd->fresh_swords, nd->held_sword_wear, ev->sv_ncry);
                                    for (int c = 0; c < ev->sv_ncry && c < NETH_RL_END_CRYSTALS; ++c)
                                        printf(" %.0f,%.0f,%.0f", ev->sv_cry[c][0], ev->sv_cry[c][1], ev->sv_cry[c][2]);
                                    printf("\n");
                                }
                            }
                            nd->seg = 1 + e->n;
                            nd->ticks = pn->ticks + nd->seg;
                            nd->wall = now_s() - S.t0;
                            nd->p = w.p;
                            pn->pend++, pn->kids++;
                            if (!e->gained) pn->gain_tries++, e->gained = 1;
                            e->best = rg, e->bprog = pg, e->gain_at = -1;
                            if (e->force_save)
                            {
                                if (resume_next) e->resume_saved = nd->id;
                                if (rollout_out)
                                {
                                    rollout_root = nd->id;
                                    e->resume_saved = nd->id;
                                    e->ending = 1;
                                }
                                printf("resume: %s's node %d is node %d (rung %d, armor worn %d, food points %d)\n", resume,
                                       e->rfrom, nd->id, rg, w.armor, food_points(&w.p.st));
                            }
                            e->force_save = 0;
                            ++S.saves;
                            if (write_seg(&S, nd, e)) printf("  node %d: its segment not written\n", nd->id);
                            else
                            {
                                char mp[2200], merr[256];
                                snprintf(mp, sizeof mp, "%s/seg/%d.mem", S.dir, nd->id);
                                if (neth_vec_chain_memory_write &&
                                    neth_vec_chain_memory_write(S.v, st, mp, merr, sizeof merr))
                                    printf("  node %d: %s\n", nd->id, merr);
                            }
                            /* (--end-seg: an End segment ends at its save, the next from the node it saved) */
                            if (S.end_seg > 0 && w.p.dim == 1) e->ending = 1;
                        }
                    }
                    else if (!gain)
                        e->gain_at = -1;
                    if (e->n >= (e->len > 0 ? e->len : S.seg_len) && !e->force_save) e->ending = 1;
                }
                if (e->ending)
                {
                    struct neth_vec_bportal bp;
                    if (S.nd[e->node].rung >= R_FLINT && neth_vec_bportal(S.v, i, &bp) == 0)
                    {
                        S.bp[0] += bp.starts, S.bp[1] += bp.sites, S.bp[2] += bp.casts, S.bp[3] += bp.lit;
                        S.bp[4] += bp.trips_lava, S.bp[5] += bp.search_steps;
                    }
                    /* the pearls' funnel (segments from home to the twelfth pearl): endermen seen, provoked, killed,
                     * pillars, deaths, the steps in the pearl macro's phases */
                    struct neth_vec_pearl pl;
                    if (S.nd[e->node].rung >= R_HOME && S.nd[e->node].rung < R_EYE12 &&
                        neth_vec_pearl(S.v, i, 1, &pl) == 0)
                    {
                        const struct neth_vec_pearl *p0 = &e->pearl0;
                        int64_t seen_new = book_delta(pl.seen, p0->seen);
                        int64_t provoked_new = book_delta(pl.provoked, p0->provoked);
                        int64_t killed_new = book_delta(pl.killed, p0->killed);
                        int64_t deaths_new = book_delta(pl.deaths, p0->deaths);
                        if (fseg)
                        {
                            fprintf(fseg, "%d\t%d\t%lld\t%lld\t%.1f\t%d\t%lld\t%d\t%d\t%d\t%d\t%d\t%d"
                                          "\t%d\t%d\t%d\t%d\t%d",
                                    i, e->node, (long long)e->n, (long long)w.p.world_time, w.p.health, w.p.food,
                                    (long long)food_points(&w.p.st), pl.pearls, pl.provoked, pl.killed, pl.hits,
                                    pl.deaths, pl.deaths ? pl.cause[0] : -1,
                                    pl.prov_end[0], pl.prov_end[1], pl.prov_end[2], pl.prov_end[3], pl.prov_end[4]);
                            fprintf(fseg, "\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n",
                                    (long long)seen_new, (long long)provoked_new, (long long)killed_new,
                                    (long long)book_delta(pl.item_steps, p0->item_steps),
                                    (long long)book_delta(pl.item_walk, p0->item_walk),
                                    (long long)book_delta(pl.home_steps, p0->home_steps),
                                    (long long)(w.p.world_time / 24000), (long long)(w.p.world_time % 24000),
                                    (long long)S.nd[e->node].p.world_time);
                            fflush(fseg);
                        }
                        S.pl[0] += 1, S.pl[1] += seen_new, S.pl[2] += provoked_new, S.pl[3] += killed_new;
                        S.pl[4] += book_delta(pl.pillars, p0->pillars), S.pl[5] += deaths_new;
                        S.pl[18] += book_delta(pl.oracle_steps, p0->oracle_steps);
                        S.pl[19] += book_delta(pl.item_steps, p0->item_steps);
                        for (int q = 0; q < 5; ++q) S.pl[61 + q] += book_delta(pl.prov_end[q], p0->prov_end[q]);
                        /* the deaths' causes (the first four of the segment): enderman, zombie, skeleton, creeper,
                         * spider, another mob, burning, drowning, starving, other */
                        for (int q = pl.deaths >= p0->deaths ? p0->deaths : 0; q < pl.deaths && q < 4; ++q)
                        {
                            int c = pl.cause[q];
                            int b = c == 7 ? 0 : c == 3 ? 1 : c == 1 ? 2 : c == 0 ? 3 : c == 2 || c == 8 ? 4
                                  : c >= 0 && c < 100 ? 5 : c == 100 ? 6 : c == 101 ? 7 : c == 102 ? 8 : 9;
                            S.pl[20 + b] += 1;
                        }
                        /* the first death's phase (pearl.c PP_*, 11 pearl_home's leg), on the leg, wet before it */
                        if (deaths_new > 0)
                        {
                            int ph = pl.death_on ? (pl.death_phase < 0 ? 0 : pl.death_phase > 11 ? 11 : pl.death_phase) : 12;
                            S.pl[30 + ph] += 1;
                            if (pl.death_leg) S.pl[43] += 1;
                            if (pl.death_wet > 0) S.pl[44] += 1;
                            /* the macro the step ran (vecenv.c M_*) */
                            S.pl[45 + (pl.death_kind < 0 ? 0 : pl.death_kind > 15 ? 15 : pl.death_kind)] += 1;
                            /* a line a first death, its segment's start node named (lane/pearlhome cases them) */
                            char prn[32];
                            rung_name(S.nd[e->node].rung, prn, sizeof prn);
                            const char *mn = pl.death_mbit == 0 ? "policy" : "?";
                            int32_t mb;
                            for (int k = 0; pl.death_mbit && neth_vec_macro_switch(k, &mb); ++k)
                                if ((uint32_t)mb == (uint32_t)pl.death_mbit) mn = neth_vec_macro_switch(k, NULL);
                            printf("  pearl death: env %d from node %d (%s), step %d, cause %d, phase %d, macro kind %d "
                                   "(%s), wet %d, deaths %d\n",
                                   i, e->node, prn, pl.death_step, pl.cause[0], pl.death_on ? pl.death_phase : -1,
                                   pl.death_kind, mn, pl.death_wet, pl.deaths);
                        }
                        for (int q = 0; q < 12; ++q)
                            S.pl[6 + q] += book_delta(pl.phase_steps[q], p0->phase_steps[q]);
                    }
                    /* the End's books (segments from end on, in the End): played, deaths by what was near (the void,
                     * the dragon, an enderman, else), crystals taken (arrows' hits, lobs'), the macro off at the end,
                     * sword hits, arrows at the dragon */
                    if (S.nd[e->node].rung >= R_END)
                    {
                        double d[NETH_VEC_DRAGON_INFO];
                        if (neth_vec_dragon_info(S.v, i, d) == 0 && d[0] == 1)
                        {
                            S.eb[0] += 1;
                            for (int q = 0; q < 4; ++q) S.eb[1 + q] += (int64_t)d[16 + q];
                            S.eb[5] += (int64_t)d[11], S.eb[6] += (int64_t)d[73], S.eb[7] += d[9] == 0;
                            S.eb[8] += (int64_t)d[20], S.eb[9] += (int64_t)d[22];
                            int dk = (int)d[25];
                            if (d[24] >= 0 && dk >= 0 && dk < 9)
                            {
                                S.ed[dk] += 1, S.ed[9] += d[26] > 0;
                                S.ed[d[24] < 3000 ? 10 : d[24] < 10000 ? 11 : d[24] < 25000 ? 12 : 13] += 1;
                                /* (a fall's length only when one was measured: s42k2-23's sum ran past int64) */
                                if (d[31] > 0 && d[31] < 400) S.ed[14] += (int64_t)d[31], S.ed[37] += 1, S.ed[38] += d[29] >= 68;
                                S.ed[15] += (int64_t)d[47];
                                int ph = (int)d[27];
                                S.ed[23 + (ph < 0 ? 0 : ph > 13 ? 13 : ph)] += 1;
                                /* (the first 400 one a line: where the End's deaths are) */
                                if (S.ed[39]++ < 400)
                                    printf("  end death: env %d node %d step %.0f cause %d phase %d pushed %.0f at %.1f %.1f %.1f "
                                           "fall %.1f head %.1f\n", i, e->node, d[24], dk, ph, d[26], d[28], d[29], d[30],
                                           d[31], d[34]);
                            }
                            if (d[21] >= 0)
                            {
                                S.ed[19] += 1;
                                S.ed[16] += d[21] <= 150, S.ed[17] += d[21] <= 100, S.ed[18] += d[21] <= 50;
                            }
                            S.ed[20] += (int64_t)d[52], S.ed[21] += (int64_t)d[51], S.ed[22] += (int64_t)d[76];
                        }
                    }
                    /* the way home's deaths (segments from rod_7 on, in the Nether): by cause, and where (from the
                     * spawner the macro remembers, and from the arrival portal) */
                    if (S.nd[e->node].rung >= R_ROD7 && S.nd[e->node].rung < R_HOME) S.hm[0] += 1;
                    struct neth_vec_food fo;
                    if (neth_vec_food(S.v, i, &fo) == 0)
                    {
                        S.fb[0] += fo.scouts, S.fb[1] += fo.scout_found;
                        for (int j = 1; j < 6; ++j) S.fb[1 + j] += fo.scout_end[j];
                        S.fb[7] += fo.hunts, S.fb[8] += fo.meat_got, S.fb[9] += fo.hunt_end[0];
                        S.fb[10] += fo.hunt_end[1], S.fb[11] += fo.hunt_end[2];
                    }
                    int k = e->resume_saved ? e->resume_saved : pick(&S, &e->rng);
                    e->resume_saved = 0;
                    if (k < 0) k = 0;
                    e->next = k;
                    S.nd[k].held++;
                    neth_vec_chain_next(S.v, i, S.nd[k].start, 0);
                    neth_vec_chain_end(S.v, i);
                    if (single_segment && e->node > 0) done = 1; /* the resume's root save is not the first live segment */
                }
            }
            else if (e->last_te < 0)
                e->last_te = te;
            env_policy(b.actions + (size_t)i * aw, aw, &e->rng, camw, keyp, keys_off);
        }
        if (rc) break;
        /* --resume (lane/chain2): a group with no env replaying is held back (received, not sent) until every replay
         * is done, so the replaying groups have the workers to themselves: one env alone replays a 258,805-tick chain
         * in 97 s (chain check), against about 15 minutes sharing the rounds with 128 envs */
        int replaying = 0, here = 0;
        for (int i = 0; i < n; ++i)
            if (E[i].replay > 0) ++replaying, here += i / gs == g;
        if (S.hold && ((replaying && !here) || (rollout_out && rollout_root < 0 && g != 0)))
        {
            held[g] = 1;
            continue;
        }
        if (rollout_out && rollout_root >= 0 && E[g * gs].rollout_done)
        {
            if (rollout_finished == n) done = 1;
            continue;
        }
        if (neth_vec_send(S.v, g))
        {
            printf("FAIL: send %d refused\n", g);
            rc = 1;
            break;
        }
        if (S.hold && !replaying && (!rollout_out || rollout_root > 0))
        {
            S.hold = 0;
            printf("resume: the replays done at %.0f s; the held groups sent\n", now_s() - S.t0);
            fflush(stdout);
            for (int q = 0; q < groups && !rc; ++q)
            {
                if (!held[q]) continue;
                if (rollout_out && q > 0)
                {
                    struct env *e = &E[q];
                    e->next = rollout_root, e->ending = 1;
                    S.nd[rollout_root].held++;
                    neth_vec_chain_next(S.v, q, S.nd[rollout_root].start, 0);
                    neth_vec_chain_end(S.v, q);
                }
                if (neth_vec_send(S.v, q)) printf("FAIL: send %d refused\n", q), rc = 1;
            }
            memset(held, 0, sizeof held);
            if (rc) break;
        }
        double t = now_s();
        if (t - tlast >= every || top_rung(&S) >= target)
        {
            tlast = t;
            bad_load(&S);
            neth_vec_stats(S.v, sst);
            double esps = (sst[0] - s0[0]) / (t - S.t0);
            frontier_table(&S, esps);
            struct rusage ru;
            getrusage(RUSAGE_SELF, &ru);
            char rn[64];
            int tr = top_rung(&S);
            rung_name(tr, rn, sizeof rn);
            printf("  %.0f s: %.0f env-steps/s, %lld segments, %d nodes (%d live), top %s, saves %lld, deaths %lld, "
                   "peak rss %.1f GB\n",
                   t - S.t0, esps, (long long)S.segs, S.nn, live_count(&S, -1), rn, (long long)S.saves,
                   (long long)S.deaths, ru.ru_maxrss / 1048576.0);
            if (S.dayff > 0)
                printf("    dayff: %d day segments playing, %lld played, %lld dead, %lld nodes saved from them\n",
                       S.day_live, (long long)S.day_segs, (long long)S.day_died, (long long)S.day_saved);
            fflush(stdout);
            if (!rollout_out && tr >= target)
            {
                int pending = 0;
                for (int k = 0; k < S.nn; ++k) pending += S.nd[k].state == NS_MAKING;
                if (!pending) done = 1;
            }
            if (minutes > 0 && t - S.t0 >= minutes * 60)
            {
                int pending = 0;
                for (int k = 0; k < S.nn; ++k) pending += S.nd[k].state == NS_MAKING;
                if (!pending) done = 1;
            }
        }
    }
    neth_vec_stats(S.v, sst);
    if (trace_out) fclose(trace_out);
    if (death_out) fclose(death_out);
    if (end_out) fclose(end_out);
    if (etrace_out) fclose(etrace_out);
    if (census_out) fclose(census_out);
    if (hunt_out) fclose(hunt_out);
    if (nlog_out) fclose(nlog_out);
    if (dlog_out) fclose(dlog_out);
    if (flog_out) fclose(flog_out);
    if (slog_out) fclose(slog_out);
    if (dtrace_out) fclose(dtrace_out);
    if (ktrace_out) fclose(ktrace_out);
    if (lvlog_out) fclose(lvlog_out);
    if (ntrace_out) fclose(ntrace_out);
    if (rodlog_out) fclose(rodlog_out);
    if (gain_out) fclose(gain_out);
    if (build_out) fclose(build_out);
    if (supply_out) fclose(supply_out);
    if (choice_out) fclose(choice_out);
    if (ladder_out) fclose(ladder_out);
    if (ptrace_out) fclose(ptrace_out);
    if (shtrace_out) fclose(shtrace_out);
    if (rollout_out)
    {
        fclose(rollout_out);
        printf("rollout: %d/%d attempts ended from one shared replay\n", rollout_finished, n);
        if (rollout_finished != n) rc = 1;
    }
    if (chooser)
    {
        printf("chooser %s: %lld steps drawn at their (bucket, kind), %lld at (bucket, policy), %lld from a lower "
               "bucket, %lld by the random driver\n", chooser_path, (long long)chooser->used[0],
               (long long)chooser->used[1], (long long)chooser->used[2], (long long)chooser->used[3]);
        for (int r = 0; r < R_N; ++r)
            for (int k = 0; k < CH_KINDS; ++k) free(chooser->a[r][k]), free(chooser->cum[r][k]);
        free(chooser);
    }
    frontier_table(&S, (sst[0] - s0[0]) / (now_s() - S.t0));
    int best = 0;
    for (int k = 0; k < S.nn; ++k)
        if (S.nd[k].state == NS_READY || S.nd[k].state == NS_FREED)
            if (S.nd[k].rung > S.nd[best].rung || (S.nd[k].rung == S.nd[best].rung && S.nd[k].ticks < S.nd[best].ticks))
                best = k;
    char rn[64];
    rung_name(S.nd[best].rung, rn, sizeof rn);
    printf("chain search done: %.0f s, best node %d (%s), chain %.1f game minutes; %s/frontier.txt\n", now_s() - S.t0,
           best, rn, S.nd[best].ticks / 1200.0, dir);
    char bp[2200];
    snprintf(bp, sizeof bp, "%s/best", dir);
    FILE *bf = fopen(bp, "w");
    if (bf) fprintf(bf, "%d\n", best), fclose(bf);
    fclose(S.ftsv);
    if (fseg) fclose(fseg);
    neth_vec_free(S.v);
    return rc;
}

/* --------------------------------------------------------- the chain's file */

static int seg_read(const char *dir, int id, struct segf *out)
{
    char p[2200];
    snprintf(p, sizeof p, "%s/seg/%d.bin", dir, id);
    FILE *f = fopen(p, "rb");
    if (!f) return fprintf(stderr, "chain: %s: %s\n", p, strerror(errno)), -1;
    if (fread(&out->h, sizeof out->h, 1, f) != 1 || (out->h.magic != SEG_MAGIC && out->h.magic != SEG_MAGIC2) ||
        out->h.step_bytes != sizeof(struct rec_step))
    {
        fclose(f);
        return fprintf(stderr, "chain: %s: not a segment of this build\n", p), -1;
    }
    out->s = malloc((size_t)(out->h.n ? out->h.n : 1) * sizeof *out->s);
    int ok;
    if (out->h.magic == SEG_MAGIC)
        ok = out->h.n == 0 || fread(out->s, sizeof *out->s, (size_t)out->h.n, f) == (size_t)out->h.n;
    else
    {
        struct seg_z z;
        ok = fread(&z, sizeof z, 1, f) == 1;
        uint8_t *zb = ok ? malloc(z.zlen ? z.zlen : 1) : NULL, *pk = ok ? malloc(z.raw ? z.raw : 1) : NULL;
        ok = ok && fread(zb, 1, z.zlen, f) == z.zlen;
        ok = ok && ZSTD_decompress(pk, z.raw, zb, z.zlen) == z.raw;
        ok = ok && rec_unpack(pk, z.raw, (uint32_t)out->h.n, z.nops, z.nexc, out->s) == 0;
        free(zb), free(pk);
    }
    fclose(f);
    return ok ? 0 : (fprintf(stderr, "chain: %s: short or bad\n", p), -1);
}

/* NODE's chain, root first: its node ids (not 0) and their segments; how many, -1 */
static int chain_load(const char *dir, int node, struct segf **out)
{
    int ids[4096], k = 0;
    for (int id = node; id > 0 && k < 4096;)
    {
        ids[k++] = id;
        struct segf s;
        if (seg_read(dir, id, &s)) return -1;
        id = s.h.parent;
        free(s.s);
    }
    *out = calloc((size_t)(k ? k : 1), sizeof **out);
    for (int j = 0; j < k; ++j)
        if (seg_read(dir, ids[k - 1 - j], &(*out)[j])) return -1;
    return k;
}

static void to_pool(const struct neth_rl_act *s, struct pool_act *a)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hold = s->hold;
    memcpy(a->press, s->press, sizeof a->press);
    a->look_mode = s->look_mode == 1 ? PL_LOOK : s->look_mode == 2 ? PL_DLOOK : PL_NONE;
    a->look[0] = s->look[0];
    a->look[1] = s->look[1];
    a->hotbar = s->hotbar;
    a->nops = s->nops < 0 ? 0 : s->nops > POOL_GUI_OPS ? POOL_GUI_OPS : s->nops;
    for (int j = 0; j < a->nops; ++j)
    {
        a->ops[j].kind = s->ops[j].kind;
        a->ops[j].window = s->ops[j].window;
        a->ops[j].slot = s->ops[j].slot;
        a->ops[j].button = s->ops[j].button;
        a->ops[j].mode = s->ops[j].mode;
        a->ops[j].index = s->ops[j].index;
    }
}

static void act_noop(struct neth_rl_act *a)
{
    memset(a, 0, sizeof *a);
    a->hotbar = -1;
}

static int cmd_path(const char *dir, int node)
{
    struct segf *c;
    int k = chain_load(dir, node, &c);
    if (k < 0) return 1;
    int maxid = 0;
    for (int j = 0; j < k; ++j) if (c[j].h.id > maxid) maxid = c[j].h.id;
    int *saved_rung = malloc(((size_t)maxid + 1) * sizeof *saved_rung);
    if (saved_rung)
    {
        for (int j = 0; j <= maxid; ++j) saved_rung[j] = -1;
        char path[2200];
        snprintf(path, sizeof path, "%s/nodes.tsv", dir);
        FILE *f = fopen(path, "r");
        if (f)
        {
            char *line = NULL;
            size_t cap = 0;
            while (getline(&line, &cap, f) > 0)
            {
                int id, parent, rung;
                if (sscanf(line, "%d\t%d\t%d", &id, &parent, &rung) == 3 && id >= 0 && id <= maxid)
                    saved_rung[id] = rung;
            }
            free(line);
            fclose(f);
        }
    }
    int64_t t = 0;
    for (int j = 0; j < k; ++j)
    {
        const struct neth_vec_chain_peek *p = &c[j].h.p;
        t += 1 + c[j].h.n;
        struct view w;
        memset(&w, 0, sizeof w);
        w.p = *p;
        w.prov = w.meat = w.carry = 1;
        w.dragon_hp = -1;
        char rn[64];
        int rung = saved_rung && c[j].h.id >= 0 && c[j].h.id <= maxid ? saved_rung[c[j].h.id] : -1;
        rung_name(rung >= 0 && rung < R_N ? rung : rung_of(&w), rn, sizeof rn);
        printf("node %d (from %d): %lld steps, chain %.1f game min, at least %s; health %.0f food %d dim %d at %.1f %.1f "
               "%.1f\n",
               c[j].h.id, c[j].h.parent, (long long)c[j].h.n, t / 1200.0, rn, p->health, p->food, p->dim, p->x, p->y,
               p->z);
    }
    free(saved_rung);
    return 0;
}

/* the items an episode record counts (vecenv.c TEACH_ITEMS then EXTRA_ITEMS, recread.c END_IDS) */
static const int16_t END_IDS[NETH_VEC_ITEMS + 8] = {
    17, 162, 5, 280, 58, 270, 271, 269, 268, 4, 274, 275, 273, 272, 263, 15, 265, 61, 3, 12, 13, 6, 260, 295, 318, 1, 14,
    264, 56, 331, 325, 259, 326, 327, 49, 257, 278, 369, 368, 381};

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    b[*len] = 0;
    fclose(f);
    return b;
}

/* the stitched chain as labelled data: one episode of ntloop --rec's format (rec.h), its steps every segment's policy
 * steps with each later seam's reset step between them (no act, trunc 1), so `ntrec tape FILE 0` writes the stitched
 * script again; the record's milestones, dimensions and items the last node's */
static int write_nrec(const char *path, const char *run, int node, const struct segf *c, int k, int argc, char **argv)
{
    int64_t S = 0;
    for (int j = 0; j < k; ++j) S += c[j].h.n + (j > 0);
    struct rec_step *st = calloc((size_t)(S ? S : 1), sizeof *st);
    int64_t t = 0;
    for (int j = 0; j < k; ++j)
    {
        if (j > 0)
        {
            st[t].act.hotbar = -1;
            st[t].trunc = 1;
            for (int a = 0; a < NETH_VEC_ACTION; ++a) st[t].action[a] = a < 2 ? NETH_VEC_CAM / 2 : 0;
            ++t;
        }
        for (int64_t u = 0; u < c[j].h.n; ++u) st[t++] = c[j].s[u];
    }
    for (int64_t u = 0; u < S; ++u) rec_canon(&st[u]);
    uint8_t *pk = malloc(rec_packed_max((uint32_t)S));
    uint32_t nops = 0, nexc = 0;
    size_t pl = rec_pack(st, (uint32_t)S, pk, &nops, &nexc);
    size_t zc = ZSTD_compressBound(pl);
    uint8_t *zb = malloc(zc);
    size_t zl = ZSTD_compress(zb, zc, pk, pl, 3);
    if (ZSTD_isError(zl)) return fprintf(stderr, "chain: zstd: %s\n", ZSTD_getErrorName(zl)), -1;
    size_t clen = 0;
    char *ctext = slurp(run, &clen);
    if (!ctext) return fprintf(stderr, "chain: %s: %s\n", run, strerror(errno)), -1;
    struct runf rf;
    memset(&rf, 0, sizeof rf);
    read_run(run, &rf);
    size_t cap = clen + 8192;
    char *text = malloc(cap);
    size_t p = (size_t)snprintf(text, cap, "config: %s\nstart: %s\nstart_path: %s\nseed: %d\nfidelity: vanilla\n"
                                           "ticks_per_step: 1\naction_repeat: 1\nrule: keep all, milestones 0x0, sample 0, "
                                           "zstd 3\nchain: node %d's chain, %d segments (lane/chain)\nargv:",
                                run, rf.start_path[0] ? "snapshot" : "spawn", rf.start_path,
                                rf.world_seed, node, k);
    for (int i = 0; i < argc && p + 512 < cap; ++i) p += (size_t)snprintf(text + p, cap - p, " %s", argv[i]);
    p += (size_t)snprintf(text + p, cap - p, "\n--- %s\n", run);
    memcpy(text + p, ctext, clen);
    p += clen;
    struct rec_head h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, REC_MAGIC, 8);
    h.version = REC_VERSION;
    h.head_bytes = sizeof h;
    h.text_bytes = (uint32_t)p;
    h.ep_bytes = sizeof(struct neth_vec_episode);
    h.step_bytes = sizeof(struct rec_step);
    h.neth_rl_version = NWRL_VERSION;
    h.n = 1, h.groups = 1, h.ep_len = (int32_t)(S + 1), h.ticks_per_step = 1, h.macros = macro_set(rf.macro_spec);
    h.vanilla = 1;
    h.seed = rf.world_seed;
    struct rec_chunk ch;
    memset(&ch, 0, sizeof ch);
    ch.magic = REC_CHUNK_MAGIC;
    ch.chunk_bytes = sizeof ch;
    ch.steps = (uint32_t)S, ch.nops = nops, ch.nexc = nexc, ch.keep = REC_KEEP_ALL;
    ch.raw_bytes = (uint32_t)pl, ch.zlen = (uint32_t)zl;
    ch.digest = rec_digest(pk, pl);
    const struct neth_vec_chain_peek *last = &c[k - 1].h.p;
    ch.rec.ms = last->ms;
    ch.rec.dims = last->dims;
    ch.rec.steps = S + 1;
    ch.rec.food = last->food;
    for (int64_t u = 0; u < S; ++u)
    {
        ch.rec.ret += (double)st[u].reward; /* ntloop's return sums the float32 rewards as float64 */
        if (st[u].kind < NETH_VEC_MACROS) ++ch.rec.macro[st[u].kind];
    }
    for (int j = 0; j < 36; ++j)
        for (int q = 0; q < NETH_VEC_ITEMS + 8; ++q)
            if (last->st.inv[j].item == END_IDS[q] && last->st.inv[j].count > 0)
            {
                if (q < NETH_VEC_ITEMS) ch.rec.items[q] += last->st.inv[j].count;
                else ch.rec.extra[q - NETH_VEC_ITEMS] += last->st.inv[j].count;
            }
    ch.rung = -1;
    ch.fid = 0;
    FILE *f = fopen(path, "wb");
    if (!f) return fprintf(stderr, "chain: %s: %s\n", path, strerror(errno)), -1;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(text, 1, p, f) == p && fwrite(&ch, sizeof ch, 1, f) == 1 &&
             fwrite(zb, 1, zl, f) == zl;
    ok &= fclose(f) == 0;
    printf("labelled data: %s, one episode of %lld steps (%zu bytes packed, %zu compressed)\n", path, (long long)S, pl, zl);
    free(st), free(pk), free(zb), free(text), free(ctext);
    return ok ? 0 : -1;
}

/* the run file's world section, the Java oracle's CONF (it reads world keys alone) */
static int write_world(const char *run, const char *out)
{
    FILE *f = fopen(run, "r"), *o = fopen(out, "w");
    if (!f || !o) return fprintf(stderr, "chain: %s or %s: %s\n", run, out, strerror(errno)), -1;
    char line[1024];
    int in = 0;
    while (fgets(line, sizeof line, f))
    {
        int ind = 0;
        while (line[ind] == ' ') ++ind;
        if (line[ind] == '#' || line[ind] == '\n' || !line[ind]) continue;
        if (ind == 2 && !strncmp(line + 2, "world:", 6)) { fputs("world:\n", o); in = 1; continue; }
        if (in && ind <= 2) break;
        if (in) fprintf(o, "  %s", line + ind);
    }
    fclose(f);
    return fclose(o);
}

static int cmd_stitch(const char *dir, int node, const char *out, const char *steps)
{
    struct segf *c;
    int k = chain_load(dir, node, &c);
    if (k < 0) return 1;
    FILE *o = fopen(out, "w");
    if (!o) return fprintf(stderr, "chain: %s: %s\n", out, strerror(errno)), 1;
    FILE *sf = steps ? fopen(steps, "wb") : NULL;
    fputs("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n", o);
    struct neth_rl_act noop;
    act_noop(&noop);
    struct pool_act a;
    int64_t ticks = 0;
    for (int j = 0; j < k; ++j)
    {
        to_pool(&noop, &a);
        moveact_script_line(o, &a);
        ++ticks;
        if (sf)
        {
            /* the segment's reset step: no act, the steps' file a row for it too (kind 0, trunc 1 marks a seam) */
            struct rec_step r;
            memset(&r, 0, sizeof r);
            r.act = noop;
            r.trunc = 1;
            fwrite(&r, sizeof r, 1, sf);
        }
        for (int64_t t = 0; t < c[j].h.n; ++t)
        {
            to_pool(&c[j].s[t].act, &a);
            moveact_script_line(o, &a);
            ++ticks;
            if (sf) fwrite(&c[j].s[t], sizeof c[j].s[t], 1, sf);
        }
    }
    if (fclose(o)) return fprintf(stderr, "chain: %s: %s\n", out, strerror(errno)), 1;
    if (sf) fclose(sf);
    printf("node %d's chain: %d segments, %lld ticks (%.1f game minutes) to %s%s%s\n", node, k, (long long)ticks,
           ticks / 1200.0, out, steps ? " and its steps to " : "", steps ? steps : "");
    return 0;
}

/* chain stitch DIR NODE -o SCRIPT [--steps FILE] [--run RUN [--nrec FILE] [--world YAML]] */
static int cmd_stitch_args(int argc, char **argv)
{
    const char *dir = argv[2], *out = NULL, *steps = NULL, *nrec = NULL, *run = NULL, *world = NULL;
    int node = atoi(argv[3]);
    for (int i = 4; i + 1 < argc; i += 2)
    {
        if (!strcmp(argv[i], "-o")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--steps")) steps = argv[i + 1];
        else if (!strcmp(argv[i], "--nrec")) nrec = argv[i + 1];
        else if (!strcmp(argv[i], "--run")) run = argv[i + 1];
        else if (!strcmp(argv[i], "--world")) world = argv[i + 1];
        else return fprintf(stderr, "chain stitch: unknown %s\n", argv[i]), 2;
    }
    if (!out || ((nrec || world) && !run)) return fprintf(stderr, "chain stitch: -o SCRIPT, and --run with --nrec or --world\n"), 2;
    int rc = cmd_stitch(dir, node, out, steps);
    if (rc) return rc;
    if (world && write_world(run, world)) return 1;
    if (world) printf("world: %s (the oracle's CONF: make -C oracle script SEED=... CONF=%s SCRIPT=%s TAPE=...)\n", world,
                      world, out);
    if (nrec)
    {
        struct segf *c;
        int k = chain_load(dir, node, &c);
        if (k <= 0 || write_nrec(nrec, run, node, c, k, argc, argv)) return 1;
    }
    return 0;
}

/* the inventory as counts by item, for a compare */
static void inv_counts(const struct neth_rl_state *s, int32_t *cnt)
{
    memset(cnt, 0, 4096 * sizeof *cnt);
    for (int j = 0; j < 36; ++j)
        if (s->inv[j].item > 0 && s->inv[j].item < 4096 && s->inv[j].count > 0) cnt[s->inv[j].item] += s->inv[j].count;
}

static int cmd_check(const char *run, const char *dir, int node, const char *rows, const char *food_ledger)
{
    struct segf *c;
    int k = chain_load(dir, node, &c);
    if (k < 0) return 1;
    struct neth_rl_opts o;
    memset(&o, 0, sizeof o);
    o.config = run;
    o.n = 1;
    o.device_mesh = -1;
    o.threads = 1;
    char err[1024];
    double t0 = now_s();
    struct nwrl *h = neth_rl_make(&o, err, sizeof err);
    if (!h) return printf("FAIL: %s\n", err), 1;
    /* --rows: every tick's row record (rowrec.h, test_snapshots --row-dump's) to PATH, to hold against the Java replay's */
    if (rows && neth_rl_rows(h, 0, rows, 0)) return printf("FAIL: --rows %s\n", rows), 1;
    FILE *food_out = food_ledger ? fopen(food_ledger, "w") : NULL;
    if (food_ledger && !food_out) return fprintf(stderr, "food ledger %s: %s\n", food_ledger, strerror(errno)), 1;
    if (food_out) fprintf(food_out, "tick\tworld_time\tx\ty\tz\tfood\tfood_delta\thealth\traw_pork\traw_beef\traw_chicken\tcooked_pork\tcooked_beef\tcooked_chicken\tdied\n");
    int id = 0, bad = 0;
    struct neth_rl_act noop;
    act_noop(&noop);
    struct neth_rl_result *res = malloc(sizeof *res);
    int64_t ticks = 0;
    static int32_t want[4096], got[4096];
    int previous[6] = {0}, totals_up[6] = {0}, totals_down[6] = {0};
    int food_rows = 0, death_rows = 0;
    int previous_food = 0;
    for (int j = 0; j < k && !bad; ++j)
    {
        for (int64_t t = -1; t < c[j].h.n; ++t)
        {
            const struct neth_rl_act *a = t < 0 ? &noop : &c[j].s[t].act;
            if (neth_rl_step(h, &id, 1, a) || neth_rl_poll(h, 1, res) != 1)
            {
                printf("FAIL: the step at tick %lld refused\n", (long long)ticks);
                return 1;
            }
            ++ticks;
            if (food_out)
            {
                const int items[6] = {IT_PORK, IT_BEEF, IT_CHICKEN, IT_PORK_C, IT_BEEF_C, IT_CHICKEN_C};
                int current[6], changed = ticks == 1;
                for (int q = 0; q < 6; ++q)
                {
                    current[q] = inv_n(&res->st, items[q]);
                    int delta = current[q] - previous[q];
                    if (delta > 0) totals_up[q] += delta;
                    if (delta < 0) totals_down[q] -= delta;
                    if (delta) changed = 1;
                    previous[q] = current[q];
                }
                int died = !!(res->flags & (PF_DIED | PF_DEAD));
                if (died) ++death_rows;
                if (changed || died)
                {
                    ++food_rows;
                    fprintf(food_out, "%lld\t%lld\t%.1f\t%.1f\t%.1f\t%d\t%d\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                            (long long)ticks, (long long)res->world_time, res->x, res->y, res->z,
                            res->food, ticks > 1 ? res->food - previous_food : 0, res->health,
                            current[0], current[1], current[2], current[3], current[4],
                            current[5], died);
                }
                previous_food = res->food;
            }
        }
        const struct neth_vec_chain_peek *p = &c[j].h.p;
        inv_counts(&c[j].h.p.st, want);
        inv_counts(&res->st, got);
        int inv_eq = !memcmp(want, got, sizeof want);
        int eq = res->x == p->x && res->y == p->y && res->z == p->z && res->health == p->health &&
                 res->food == p->food && res->dim == p->dim && inv_eq;
        printf("node %d: tick %lld, %s (x %.3f %.3f y %.3f %.3f z %.3f %.3f health %.1f %.1f food %d %d dim %d %d, "
               "inventory %s)\n",
               c[j].h.id, (long long)ticks, eq ? "equal" : "DIFFERS", res->x, p->x, res->y, p->y, res->z, p->z,
               res->health, p->health, res->food, p->food, res->dim, p->dim, inv_eq ? "equal" : "differs");
        fflush(stdout);
        if (!eq) bad = 1;
    }
    printf("CHECK %s: node %d's chain, %d segments, %lld ticks played from the run's start in %.1f s\n",
           bad ? "DIFF" : "EQUAL", node, k, (long long)ticks, now_s() - t0);
    if (food_out)
    {
        fprintf(food_out, "# inventory increases raw pork/beef/chicken %d/%d/%d cooked %d/%d/%d; decreases raw %d/%d/%d cooked %d/%d/%d; change rows %d, death rows %d\n",
                totals_up[0], totals_up[1], totals_up[2], totals_up[3], totals_up[4], totals_up[5],
                totals_down[0], totals_down[1], totals_down[2], totals_down[3], totals_down[4], totals_down[5],
                food_rows, death_rows);
        fclose(food_out);
    }
    if (rows) neth_rl_rows(h, 0, NULL, 0);
    neth_rl_free(h);
    return bad;
}

/* the Java client's last row against the node's state: the inventory's counts by item */
static int cmd_end(const char *dir, int node, const char *tape)
{
    struct segf s;
    if (seg_read(dir, node, &s)) return 1;
    FILE *f = fopen(tape, "r");
    if (!f) return fprintf(stderr, "chain: %s: %s\n", tape, strerror(errno)), 1;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    long rows = 0;
    while (getline(&line, &cap, f) > 0)
    {
        if (line[0] != '{' || !strncmp(line, "{\"tape\"", 7)) continue;
        ++rows;
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    struct jval *row = last ? json_parse(last) : NULL;
    const struct jval *inv = row ? json_get(json_get(row, "sp"), "inv") : NULL;
    int64_t t = -1;
    if (!row || !json_int(json_get(row, "t"), &t) || !inv) return printf("end: %s has no row with an inventory\n", tape), 1;
    static int32_t want[4096];
    static int64_t java[4096];
    inv_counts(&s.h.p.st, want);
    for (int k = 0; k < json_len(inv); ++k)
    {
        const struct jval *e = json_at(inv, k);
        int64_t item = -1, cnt = 0;
        if (!json_int(json_at(e, 1), &item) || !json_int(json_at(e, 2), &cnt) || item <= 0 || item >= 4096) continue;
        java[item] += cnt;
    }
    int diff = 0;
    /* the server player's place, health, food and dimension */
    const struct jval *sp = json_get(row, "sp");
    double jx = 0, jy = 0, jz = 0, jhp = 0;
    int64_t jfood = -1, jdim = -9;
    const struct jval *q;
    if ((q = json_get(sp, "x"))) jx = strtod(q->raw, NULL);
    if ((q = json_get(sp, "y"))) jy = strtod(q->raw, NULL);
    if ((q = json_get(sp, "z"))) jz = strtod(q->raw, NULL);
    if ((q = json_get(sp, "hp"))) jhp = strtod(q->raw, NULL);
    json_int(json_get(sp, "food"), &jfood);
    json_int(json_get(sp, "dim"), &jdim);
    const struct neth_vec_chain_peek *p = &s.h.p;
    int peq = jx == p->x && jy == p->y && jz == p->z && (float)jhp == p->health && jfood == p->food && jdim == p->dim;
    printf("end: the player: sim x %.6f y %.6f z %.6f health %.1f food %d dim %d, java x %.6f y %.6f z %.6f health %.1f "
           "food %lld dim %lld: %s\n",
           p->x, p->y, p->z, p->health, p->food, p->dim, jx, jy, jz, jhp, (long long)jfood, (long long)jdim,
           peq ? "equal" : "DIFFERS");
    diff |= !peq;
    printf("end: node %d against the Java client's last row (t %lld, %ld rows):", node, (long long)t, rows);
    int idiff = 0;
    for (int id = 1; id < 4096; ++id)
        if (java[id] != want[id]) printf(" item %d sim %d java %lld", id, want[id], (long long)java[id]), idiff = 1;
    if (!idiff) printf(" every item's count equal");
    diff |= idiff;
    printf("\n");
    for (int id = 1; id < 4096; ++id)
        if (want[id]) printf("  holds %d x%d\n", id, want[id]);
    printf("END %s\n", diff ? "DIFF" : "EQUAL");
    json_free(row);
    return diff;
}

/* chain script-check (lane/chain2): the script writer (moveact_script_line) round-trips every look and dlook number,
 * -0.0 and floats that need 9 digits among them, as the Java oracle reads them (Gson 2.2.4 reads "-0" as the long 0: the
 * number must carry a point or an exponent when it is -0.0). SCRIPT-CHECK PASS or FAIL */
static int cmd_script_check(void)
{
    static const float V[] = {-0.0f, 0.0f, 1.0f, -1.0f, 0.1f, -28.2760792f, 3.40282347e38f, 1.17549435e-38f,
                              -1.40129846e-45f, 90.0f, -90.0f, 359.999969f};
    int bad = 0, n = 0;
    for (int mode = 0; mode < 2; ++mode)
        for (size_t i = 0; i < sizeof V / sizeof *V; ++i)
        {
            struct pool_act a;
            memset(&a, 0, sizeof a);
            a.kind = PA_AGENT;
            a.hotbar = -1;
            a.look_mode = mode ? PL_DLOOK : PL_LOOK;
            a.look[0] = V[i], a.look[1] = V[(i + 1) % (sizeof V / sizeof *V)];
            char buf[512] = "";
            FILE *f = fmemopen(buf, sizeof buf - 1, "w");
            moveact_script_line(f, &a);
            fclose(f);
            const char *p = strstr(buf, mode ? "\"dlook\":[" : "\"look\":[");
            if (!p) { printf("no look in %s", buf); ++bad; continue; }
            p = strchr(p, '[') + 1;
            for (int k = 0; k < 2; ++k)
            {
                char *e;
                float got = strtof(p, &e);
                char num[64];
                snprintf(num, sizeof num, "%.*s", (int)(e - p), p);
                int isint = !strpbrk(num, ".eE");
                ++n;
                if (memcmp(&got, &a.look[k], 4) || (got == 0.0f && signbit(got) && isint))
                {
                    printf("SCRIPT-CHECK: %s %a written %s\n", mode ? "dlook" : "look", (double)a.look[k], num);
                    ++bad;
                }
                p = e + 1;
            }
        }
    printf("SCRIPT-CHECK %s: %d numbers written and read back\n", bad ? "FAIL" : "PASS", n);
    return bad != 0;
}

static int cmd_fortress_list(int64_t seed, double x, double z, double radius)
{
    if (radius < 0 || radius > 2000) return 2;
    structure_fortress.begin(seed);
    int count = 0;
    for (int cx = (int)floor((x - radius) / 16.0); cx <= (int)floor((x + radius) / 16.0); ++cx)
        for (int cz = (int)floor((z - radius) / 16.0); cz <= (int)floor((z + radius) / 16.0); ++cz)
        {
            jrand rand;
            if (!structure_fortress.can_spawn(&rand, cx, cz)) continue;
            double sx = cx * 16.0 + 8.0, sz = cz * 16.0 + 8.0;
            double sd = hypot(sx - x, sz - z);
            if (sd > radius) continue;
            struct start s;
            start_init(&s, "Fortress", cx, cz);
            structure_fortress.make_start(&rand, cx, cz, &s);
            double best = 1e30, throne = 1e30;
            int px = 0, py = 0, pz = 0, tx = 0, ty = 0, tz = 0;
            for (int i = 0; i < s.n; ++i)
            {
                const struct piece *p = s.pieces[i];
                const struct bbox *b = &p->bb;
                double dx = x < b->minX ? b->minX - x : x > b->maxX + 1 ? x - b->maxX - 1 : 0.0;
                double dz = z < b->minZ ? b->minZ - z : z > b->maxZ + 1 ? z - b->maxZ - 1 : 0.0;
                double d = hypot(dx, dz);
                if (d < best)
                    best = d, px = (b->minX + b->maxX) / 2, py = b->minY, pz = (b->minZ + b->maxZ) / 2;
                if (p->u.fortress.kind == F_THRONE && d < throne)
                    throne = d, tx = (b->minX + b->maxX) / 2, ty = b->minY, tz = (b->minZ + b->maxZ) / 2;
            }
            printf("candidate seed %lld start %d %d centre %.0f %.0f start_d %.1f nearest_piece %d %d %d piece_d %.1f "
                   "throne %d %d %d throne_d %.1f pieces %d\n",
                   (long long)seed, cx, cz, sx, sz, sd, px, py, pz, best, tx, ty, tz,
                   throne == 1e30 ? -1.0 : throne, s.n);
            start_free(&s);
            ++count;
        }
    printf("candidate-count %d within %.1f blocks of %.1f %.1f\n", count, radius, x, z);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "script-check")) return cmd_script_check();
    if (argc == 6 && !strcmp(argv[1], "fortress-list"))
        return cmd_fortress_list(strtoll(argv[2], NULL, 10), strtod(argv[3], NULL), strtod(argv[4], NULL),
                                 strtod(argv[5], NULL));
    if (argc == 5 && !strcmp(argv[1], "fortress-locate"))
    {
        int64_t seed = strtoll(argv[2], NULL, 10);
        double x = strtod(argv[3], NULL), z = strtod(argv[4], NULL);
        int cx = 0, cz = 0;
        if (!fortress_locate_start(seed, (int)floor(x), (int)floor(z), 8, &cx, &cz))
            return fprintf(stderr, "no candidate start within eight regions\n"), 1;
        double bx = cx * 16.0 + 8.0, bz = cz * 16.0 + 8.0;
        printf("fortress-start seed %lld arrival %.1f %.1f chunk %d %d centre %.1f %.1f distance %.1f\n",
               (long long)seed, x, z, cx, cz, bx, bz, hypot(bx - x, bz - z));
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "search")) return cmd_search(argc, argv);
    if (argc == 6 && !strcmp(argv[1], "seg-cmp"))
    {
        /* lane/fwd: two --crossings directories' steps (seg/1.bin) compared over [FROM, TO): the act each step
         * stepped, the macro kind, chosen and driver; SEG EQUAL or the first differing step */
        struct segf a, b;
        if (seg_read(argv[2], 1, &a) || seg_read(argv[3], 1, &b)) return 2;
        int64_t from = atoll(argv[4]), to = atoll(argv[5]);
        if (from < 0 || to > a.h.n || to > b.h.n || from >= to)
            return fprintf(stderr, "seg-cmp: [%lld, %lld) outside %lld and %lld steps\n", (long long)from, (long long)to,
                           (long long)a.h.n, (long long)b.h.n), 2;
        for (int64_t t = from; t < to; ++t)
        {
            rec_canon(&a.s[t]), rec_canon(&b.s[t]);
            if (memcmp(&a.s[t].act, &b.s[t].act, sizeof a.s[t].act) || a.s[t].kind != b.s[t].kind ||
                a.s[t].chosen != b.s[t].chosen || a.s[t].driver != b.s[t].driver)
            {
                printf("SEG DIFF at step %lld (kind %d %d, chosen %d %d, driver %d %d)\n", (long long)t, a.s[t].kind,
                       b.s[t].kind, a.s[t].chosen, b.s[t].chosen, a.s[t].driver, b.s[t].driver);
                return 1;
            }
        }
        printf("SEG EQUAL %lld steps [%lld, %lld)\n", (long long)(to - from), (long long)from, (long long)to);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "path")) return cmd_path(argv[2], atoi(argv[3]));
    if (argc >= 6 && !strcmp(argv[1], "stitch")) return cmd_stitch_args(argc, argv);
    if ((argc == 6 || (argc == 8 && (!strcmp(argv[6], "--rows") || !strcmp(argv[6], "--food-ledger")))) && !strcmp(argv[1], "check") &&
        !strcmp(argv[2], "--run"))
        return cmd_check(argv[3], argv[4], atoi(argv[5]), argc == 8 && !strcmp(argv[6], "--rows") ? argv[7] : NULL,
                         argc == 8 && !strcmp(argv[6], "--food-ledger") ? argv[7] : NULL);
    if (argc == 5 && !strcmp(argv[1], "end")) return cmd_end(argv[2], atoi(argv[3]), argv[4]);
    fprintf(stderr, "usage: chain search --run FILE --dir DIR [...] | fortress-locate SEED X Z | "
                    "fortress-list SEED X Z RADIUS | path DIR NODE | "
                    "stitch DIR NODE -o SCRIPT [--steps FILE] | check --run FILE DIR NODE [--food-ledger TSV] | end DIR NODE TAPE\n");
    return 2;
}
