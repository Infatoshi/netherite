/* A seeded model of a human player, written as an out/native/play input
 * script (JSON lines of timed SDL events, play.c's script_key and the events
 * listed above it). The same SEED and TICKS always give the same script.
 *
 *   out/native/playfuzz_gen SEED [--ticks N] [--t0 2] > script.jsonl
 *   out/native/playfuzz_gen SEED --pose "X Y Z YAW PITCH DIM" --kit KIT.jsonl --actions ACTS.tsv > script.jsonl
 *
 * --pose is the player at the start (the snapshot's row 1: sp x y z, cp yaw
 * pitch, sp dim). With it, about three sessions in five get a kit: the items
 * of the speedrun actions (ender pearls, eyes of ender, drinkable and splash
 * potions, a fishing rod, a boat, a minecart, TNT with flint and steel, an
 * enchanting table, an anvil, a brewing stand, a saddle with a carrot on a
 * stick, leads, emeralds and trade goods) put in hotbar and inventory slots,
 * and at most one staged scene in front of the player (a Nether portal frame
 * to light and walk into, an End portal ring missing one eye, a villager to
 * trade with, a pig to saddle and ride, an animal to lead to a fence post, a
 * rail for the minecart). The kit is Dev header entries at the snapshot tick
 * (tick 2), written to KIT.jsonl for out/native/play-dev --dev-ops: the dev
 * give path play already has (the starts hold no boat, TNT or potion). A
 * session without a kit is the script the model wrote before kits, draw for
 * draw (the kit's own Random decides). ACTS.tsv lists each speedrun action
 * the script attempts, with its tick (playfuzz_coverage counts them).
 *
 * The model is open loop: it does not see the game, so it plays like someone
 * not looking at the screen. A session is a run of episodes drawn by weight:
 * walking and sprinting with the mouse drifting, jumps, sneaking, looking
 * around and spinning, mining (held attack), fighting (click spam while
 * strafing), using what the crosshair hits (taps and holds: blocks, doors,
 * chests, beds, buckets, flint and steel, food, bows), towers (look down, jump,
 * place), digging straight down, bridging backwards while sneaking, hotbar keys
 * and the wheel, dropping (q, and with ctrl held), the inventory or whatever
 * screen is open (left, right and middle clicks on slots, shift clicks, number
 * keys and q over a slot, drags, double clicks, clicks outside), pausing,
 * respawning (r and the button), leaving a bed; with a kit, the speedrun
 * actions above (selecting the item's hotbar slot, then acting on it, aimed
 * where the scene is). The session ends with a quit, often with keys still
 * held. Its draws are Python's random.Random(SEED).random(), so the scripts
 * are the ones gen.py wrote before its port to C (lane/cport). */
#define _GNU_SOURCE
#include "playgen.h"

static const char *USAGE =
    "usage: playfuzz_gen SEED [--ticks N] [--t0 2] [--pose \"X Y Z YAW PITCH DIM\" --kit KIT.jsonl --actions ACTS.tsv]\n";

int main(int argc, char **argv)
{
    long long seed = 0, ticks = -1, t0 = 2;
    int have_seed = 0, npose = 0;
    double pose[6];
    const char *kit_path = NULL, *acts_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--pose") && i + 1 < argc) npose = parse_pose(argv[++i], pose);
        else if (!strcmp(argv[i], "--kit") && i + 1 < argc) kit_path = argv[++i];
        else if (!strcmp(argv[i], "--actions") && i + 1 < argc) acts_path = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--t0") && i + 1 < argc) t0 = atoll(argv[++i]);
        else {
            char *e;
            seed = strtoll(argv[i], &e, 10);
            if (*e || e == argv[i]) die("playfuzz_gen: not a seed: %s", argv[i]);
            have_seed = 1;
        }
    }
    if (!have_seed) {
        fputs(USAGE, stderr);
        return 2;
    }
    if (ticks < 0) ticks = default_ticks(seed);
    slot_points();
    struct kit kit;
    if (npose) kit_init(&kit, seed, pose);
    struct gen g;
    gen_init(&g, seed, ticks, t0, npose ? &kit : NULL);
    gen_run(&g);
    write_rows(&g.out);
    fflush(stdout);
    if (kit_path && write_kit(kit_path, &g, &kit)) return 1;
    if (acts_path) {
        FILE *f = fopen(acts_path, "w");
        if (!f) die("playfuzz_gen: cannot write %s", acts_path);
        if (g.kit) {
            fprintf(f, "kit\t%d\n", 2);
            if (kit.stage) fprintf(f, "stage_%s\t2\n", kit.stage);
        }
        for (int i = 0; i < g.acts.n; ++i) fprintf(f, "%s\n", g.acts.v[i]);
        fclose(f);
    }
    return 0;
}
