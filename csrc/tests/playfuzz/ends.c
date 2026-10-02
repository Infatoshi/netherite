/* Where a chained fuzz session ends: playfuzz_gen's session (the same SEED
 * plays the same episodes) with its last moments chosen so the Save and Quit
 * that starts the next session lands inside a state a restore could lose.
 *
 *   out/native/playfuzz_ends SEED [--end KIND] [--use-slot K] [--throw-slot K] [--inv INV]
 *       [--pose "X Y Z YAW PITCH DIM" --kit KIT.jsonl] [--ticks N] [--t0 2] > script.jsonl
 *   out/native/playfuzz_ends --kinds          the kinds, one per line
 *   out/native/playfuzz_ends SEED --which     the kind SEED draws
 *
 * KIND (drawn from SEED when not given; the session's own draws are untouched):
 *   gen        the session's ending: a third release everything, the rest quit with keys held
 *   screen     the inventory open, often with a stack on the cursor or in the grid
 *   container  a chest, a furnace or a workbench placed and opened, clicked, left open; with
 *              none to place, whatever the crosshair opens
 *   drop       q (and ctrl q) just before the quit: the item still in the air
 *   throw      a right click or a short bow draw let go just before the quit: a thrown item in flight
 *   fight      click spam while strafing, the quit mid-swing
 *   dig        attack held on a block, the quit mid-dig
 *   use        use held (eating, drinking, a drawn bow), the quit mid-use
 *   air        a sprint jump, the quit in the air
 *   tower      a jump and a place under the feet, the quit before landing
 *   bed        a right click on what is in front (a bed puts the player in it), the quit in bed
 * The body is the session's run up to its length; the end kind's episode
 * then runs past it and the quit comes where the episode stops. --use-slot
 * and --throw-slot (1 to 9, from the session's start inventory: chain.sh) are
 * the hotbar keys of something usable (food, a bow with arrows) and of
 * something thrown (a snowball, an egg, a pearl, a bottle o' enchanting, an
 * eye); the use and throw ends press them first. --inv (chain.sh, the start's
 * inventory as SLOT:ID:COUNT,...; slots 0 to 8 the hotbar) lets the container
 * end bring its block: in the session's first ticks it moves a chest, a
 * furnace or a workbench from the inventory to the hotbar, or crafts one (a
 * workbench from four planks in the inventory's grid; a chest from eight
 * planks or a furnace from eight cobblestone in that workbench, placed in
 * front and opened), keeps it in an empty main slot (away from the body's
 * use, tower and drop), and at the end brings it back to the hotbar, places
 * it (under the feet in a jump, else on the ground ahead) and opens it. The
 * prelude has its own Random: the body is the session's draw for draw,
 * shifted by the prelude's ticks. --pose and --kit are playfuzz_gen's: a kit
 * session (its Dev entries written to KIT.jsonl, for play-dev --dev-ops) gets
 * the kit's scene and speedrun actions; its container prelude starts after
 * the scene, over the start's inventory with the kit's slots put in. The C
 * port of ends.py (lane/cport): the same scripts byte for byte. */
#define _GNU_SOURCE
#include "playgen.h"

static const struct {
    int w;
    const char *k;
} ENDS[] = {{4, "gen"}, {3, "screen"}, {2, "container"}, {2, "drop"}, {2, "throw"}, {2, "fight"},
            {2, "dig"}, {2, "use"}, {2, "air"}, {1, "tower"}, {1, "bed"}};
#define NENDS ((int)(sizeof ENDS / sizeof *ENDS))

enum { PLANKS = 5, COBBLE = 4, WORKBENCH = 58, CHEST = 54, FURNACE = 61 };
static const int RING[8] = {0, 1, 2, 3, 5, 6, 7, 8}; /* the workbench grid but its centre: a chest, a furnace */

/* an inventory slot (0 to 8 the hotbar, 9 to 35 the rest) where the inventory
 * and the workbench (and the furnace) draw it */
static struct pt slot_xy(int s) { return PLAYER_SLOTS[s < 9 ? 36 + s : s]; }

/* the start's inventory: slot -> (id, count), -1 where empty */
struct inv {
    int set, id[36], count[36];
};

static int use_slot, throw_slot, cont_key, stash_slot = -1;
static struct inv inv;

/* pick the stack at inventory slot src up, one into each cell, the rest back */
static void put_ring(struct gen *g, int src, const int *cells, int n, const struct pt *points)
{
    struct pt s = slot_xy(src);
    pointer(g, s.x, s.y);
    click(g, 1, s.x, s.y, 0);
    gwait(g, 1);
    for (int i = 0; i < n; ++i) {
        s = points[cells[i]];
        pointer(g, s.x, s.y);
        click(g, 2, s.x, s.y, 0);
        gwait(g, 1);
    }
    s = slot_xy(src);
    pointer(g, s.x, s.y);
    click(g, 1, s.x, s.y, 0);
    gwait(g, 1);
}

/* a number key over a slot: its stack to hotbar key */
static void to_hotbar(struct gen *g, struct pt s, int key_)
{
    pointer(g, s.x, s.y);
    gwait(g, 1);
    tap_n(g, key_, 0);
    gwait(g, gi(g, 1, 3));
}

/* in an open player-slot screen: the block from its hotbar slot into an empty main slot */
static void stash(struct gen *g)
{
    int free_[27], nf = 0;
    for (int s = 9; s < 36; ++s)
        if (!inv.set || inv.id[s] < 0) free_[nf++] = s;
    if (!nf) return;
    stash_slot = free_[pick_i(g, nf)];
    int two[2] = {cont_key - 1, stash_slot};
    for (int i = 0; i < 2; ++i) {
        struct pt s = slot_xy(two[i]);
        pointer(g, s.x, s.y);
        click(g, 1, s.x, s.y, 0);
        gwait(g, 1);
    }
}

static int find(int item)
{
    int best = -1;
    for (int s = 0; s < 36; ++s)
        if (inv.set && inv.id[s] == item && inv.count[s] >= 1 && (best < 0 || inv.count[s] > inv.count[best])) best = s;
    return best;
}

/* bring a container block to the hotbar (moved, or crafted), the session's first ticks */
static void prelude_container(struct gen *g)
{
    int wb = find(WORKBENCH), chest = find(CHEST), furnace = find(FURNACE), planks = find(PLANKS), cobble = find(COBBLE);
    int np = planks >= 0 ? inv.count[planks] : 0, nc = cobble >= 0 ? inv.count[cobble] : 0;
    int can_wb = wb >= 0 || np >= 4, need = wb >= 0 ? 0 : 4;
    const char *kinds[3];
    int nk = 0;
    if (chest >= 0 || (can_wb && np - need >= 8)) kinds[nk++] = "chest";
    if (furnace >= 0 || (can_wb && nc >= 8 && np >= need)) kinds[nk++] = "furnace";
    if (can_wb) kinds[nk++] = "workbench";
    if (!nk) return;
    const char *kind = kinds[pick_i(g, nk)];
    int free_[9], nf = 0, keys[12], nkeys = 0, kpos = 0;
    for (int k = 0; k < 9; ++k)
        if (!inv.set || inv.id[k] < 0) free_[nf++] = k;
    for (int i = nf - 1; i >= 0; --i) keys[nkeys++] = free_[i]; /* hotbar keys to fill, from the right */
    for (int k = 8; k >= 6; --k) {
        int in = 0;
        for (int i = 0; i < nf; ++i) in |= free_[i] == k;
        if (!in) keys[nkeys++] = k;
    }
    int have = !strcmp(kind, "chest") ? chest : !strcmp(kind, "furnace") ? furnace : wb;
    gwait(g, gi(g, 0, 3));
    tap(g, "e", 0);
    gwait(g, gi(g, 2, 4));
    if (have >= 0) {
        if (have < 9) cont_key = have + 1; /* in the hotbar already */
        else {                             /* in the inventory: a number key over it */
            cont_key = keys[0] + 1;
            to_hotbar(g, slot_xy(have), cont_key);
        }
        stash(g);
        tap(g, "e", 0);
        gwait(g, gi(g, 1, 3));
        return;
    }
    int wkey = wb >= 0 && wb < 9 ? wb + 1 : -1;
    if (wb < 0) { /* a workbench from four planks in the 2x2 grid */
        static const int GRID[4] = {1, 2, 3, 4};
        put_ring(g, planks, GRID, 4, PLAYER_SLOTS);
        wkey = keys[kpos++] + 1;
        to_hotbar(g, PLAYER_SLOTS[0], wkey);
        np -= 4;
    } else if (wkey < 0) {
        wkey = keys[kpos++] + 1;
        to_hotbar(g, slot_xy(wb), wkey);
    }
    if (!strcmp(kind, "workbench")) {
        cont_key = wkey;
        stash(g);
        tap(g, "e", 0);
        gwait(g, gi(g, 1, 3));
        return;
    }
    tap(g, "e", 0);
    gwait(g, gi(g, 1, 3));
    /* the workbench in front, opened: eight planks (a chest) or cobblestone (a furnace) */
    tap_n(g, wkey, 0);
    gwait(g, gi(g, 1, 3));
    double yaw = g->yaw, pitch = g->pitch;
    look(g, yaw, u(g, 35, 55)); /* the ground a block or more ahead, not under the feet */
    click_c(g, 2, 0);
    gwait(g, gi(g, 4, 7));
    click_c(g, 2, 0);
    gwait(g, gi(g, 3, 5));
    put_ring(g, !strcmp(kind, "chest") ? planks : cobble, RING, 8, ALL_SLOTS + 46);
    cont_key = keys[kpos++] + 1;
    to_hotbar(g, ALL_SLOTS[45], cont_key);
    stash(g); /* the workbench screen draws the player slots where the inventory does */
    tap(g, "e", 0);
    gwait(g, gi(g, 1, 3));
    look(g, yaw, pitch);
    gwait(g, gi(g, 0, 3));
}

/* leave a stack on the cursor (a left click on a player slot), or items in the 2x2 grid */
static void cursor_stack(struct gen *g)
{
    if (p(g, 0.6)) {
        struct pt s = PLAYER_SLOTS[5 + pick_i(g, 40)];
        pointer(g, s.x, s.y);
        click(g, 1, s.x, s.y, 0);
        if (p(g, 0.4)) { /* and one of it put in the grid */
            s = PLAYER_SLOTS[1 + pick_i(g, 4)];
            pointer(g, s.x, s.y);
            click(g, 2, s.x, s.y, 0);
        }
        gwait(g, gi(g, 0, 3));
    }
}

/* whatever the cut-off body left open closed: escape shuts a screen or opens
 * the pause menu, and Back to Game in the same tick lifts that */
static void in_game(struct gen *g)
{
    tap(g, "escape", 0);
    click(g, 1, SW / 2, SH / 4 + 34, 0);
    gwait(g, gi(g, 2, 4));
}

static void end_screen(struct gen *g)
{
    if (p(g, 0.5)) release_all(g);
    in_game(g);
    tap(g, "e", 0);
    gwait(g, gi(g, 2, 8));
    screen_actions(g, gi(g, 0, 6));
    cursor_stack(g);
    gwait(g, gi(g, 0, 5));
}

static void end_container(struct gen *g)
{
    if (cont_key) { /* stand still, place the prelude's block in front, open it */
        static const char *const K[7] = {"w", "a", "s", "d", "space", "shift", "ctrl"};
        for (int i = 0; i < 7; ++i) key(g, K[i], 0);
        for (int b = 1; b <= 2; ++b) mouse_c(g, b, 0);
        in_game(g);
        gwait(g, gi(g, 6, 12));
        if (stash_slot >= 0) { /* back from its main slot to the hotbar key */
            tap(g, "e", 0);
            gwait(g, gi(g, 2, 4));
            to_hotbar(g, slot_xy(stash_slot), cont_key);
            tap(g, "e", 0);
            gwait(g, gi(g, 1, 3));
        }
        tap_n(g, cont_key, 0);
        gwait(g, gi(g, 1, 3));
        look(g, g->yaw, u(g, 86, 90));
        tap(g, "space", 0);
        gwait(g, gi(g, 3, 5));
        click_c(g, 2, 0);
        gwait(g, gi(g, 6, 9));
        click_c(g, 2, 0);
        gwait(g, gi(g, 4, 6));
        double a = g->yaw + u(g, -30, 30);
        look(g, a, u(g, 35, 55));
        click_c(g, 2, 0);
        gwait(g, gi(g, 4, 8));
        click_c(g, 2, 0);
    } else {
        double a = g->yaw + u(g, -30, 30);
        look(g, a, u(g, -10, 60));
        click_c(g, 2, 0);
    }
    gwait(g, gi(g, 2, 8));
    screen_actions(g, gi(g, 0, 6));
    if (p(g, 0.5)) cursor_stack(g);
    gwait(g, gi(g, 0, 5));
}

static void end_drop(struct gen *g)
{
    if (p(g, 0.5)) ep_hotbar(g);
    long long n = gi(g, 1, 3);
    for (long long i = 0; i < n; ++i) {
        if (p(g, 0.3)) {
            key(g, "ctrl", 1);
            tap(g, "q", 0);
            key(g, "ctrl", 0);
        } else tap(g, "q", gi(g, 0, 1));
        gwait(g, gi(g, 0, 3));
    }
    gwait(g, gi(g, 0, 4));
}

static void end_throw(struct gen *g)
{
    if (throw_slot) {
        tap_n(g, throw_slot, gi(g, 0, 1));
        gwait(g, gi(g, 1, 3));
    } else ep_hotbar(g);
    double a = g->yaw + u(g, -40, 40);
    look(g, a, u(g, -40, 20));
    if (p(g, 0.5)) click_c(g, 2, gi(g, 0, 1));
    else { /* a bow let go after a short draw */
        mouse_c(g, 2, 1);
        gwait(g, gi(g, 5, 25));
        mouse_c(g, 2, 0);
    }
    gwait(g, gi(g, 0, 5));
}

static void end_fight(struct gen *g)
{
    static const char *const S[3] = {"a", "d", NULL};
    const char *strafe = PICK(g, S);
    if (strafe) key(g, strafe, 1);
    if (p(g, 0.5)) key(g, "w", 1);
    long long n = gi(g, 1, 6);
    for (long long i = 0; i < n; ++i) {
        click_c(g, 1, gi(g, 0, 2));
        long long d = gi(g, 1, 8);
        drift(g, d, u(g, 10, 80));
    }
    if (p(g, 0.5)) {
        mouse_c(g, 1, 1);
        gwait(g, gi(g, 0, 2));
    }
}

static void end_dig(struct gen *g)
{
    double a = g->yaw + u(g, -40, 40);
    look(g, a, u(g, -10, 85));
    mouse_c(g, 1, 1);
    long long d = gi(g, 3, 60);
    drift(g, d, u(g, 0, 4));
}

static void end_use(struct gen *g)
{
    if (use_slot) {
        tap_n(g, use_slot, gi(g, 0, 1));
        gwait(g, gi(g, 1, 3));
    } else if (p(g, 0.5)) ep_hotbar(g);
    mouse_c(g, 2, 1);
    long long d = gi(g, 3, 40);
    drift(g, d, u(g, 0, 10));
}

static void end_air(struct gen *g)
{
    key(g, "ctrl", 1);
    key(g, "w", 1);
    gwait(g, gi(g, 2, 15));
    tap(g, "space", gi(g, 0, 2));
    gwait(g, gi(g, 1, 10));
}

static void end_tower(struct gen *g)
{
    look(g, g->yaw, u(g, 80, 90));
    tap(g, "space", gi(g, 0, 1));
    gwait(g, gi(g, 2, 6));
    click_c(g, 2, 0);
    gwait(g, gi(g, 0, 3));
}

static void end_bed(struct gen *g)
{
    double a = g->yaw + u(g, -30, 30);
    look(g, a, u(g, 20, 70));
    click_c(g, 2, 0);
    gwait(g, gi(g, 5, 120));
}

static void prelude_own_random(struct gen *g, long long seed)
{
    struct pyrand pr;
    pr_seed_int(&pr, seed * 7907 + 3);
    struct pyrand *r = g->r;
    g->r = &pr;
    prelude_container(g);
    g->r = r;
}

static void run_end(struct gen *g, const char *kind, long long seed)
{
    int container = !strcmp(kind, "container");
    if (container && !g->kit) prelude_own_random(g, seed); /* its own Random: the body's draws stay the session's */
    gwait(g, gi(g, 0, 5));
    if (g->kit) { /* a kit session: its scene, then the speedrun actions too */
        if (g->t < 5) g->t = 5;
        g->yaw = g->kit->yaw, g->pitch = g->kit->pitch;
        if (g->kit->stage) st_run(g, g->kit->stage);
        if (container) { /* the kit's slots over the start's inventory, then the prelude */
            if (!inv.set) {
                inv.set = 1;
                for (int s = 0; s < 36; ++s) inv.id[s] = -1;
            }
            for (int i = 0; i < g->kit->nops; ++i) {
                const struct kitop *o = &g->kit->ops[i];
                if (o->is_slot && 0 <= o->slot && o->slot < 36) inv.id[o->slot] = o->item, inv.count[o->slot] = o->count;
            }
            prelude_own_random(g, seed);
        }
    }
    while (g->t < g->end) {
        episode_step(g);
        if (p(g, 0.1)) release_all(g);
    }
    if (!strcmp(kind, "gen")) {
        if (p(g, 1.0 / 3)) {
            g->t = g->end - 1;
            release_all(g);
        }
        g->t = g->end;
    } else {
        /* the end episode runs past the session's length; the quit comes where it stops */
        g->t = g->end;
        g->end = 1LL << 40;
        if (!strcmp(kind, "screen")) end_screen(g);
        else if (container) end_container(g);
        else if (!strcmp(kind, "drop")) end_drop(g);
        else if (!strcmp(kind, "throw")) end_throw(g);
        else if (!strcmp(kind, "fight")) end_fight(g);
        else if (!strcmp(kind, "dig")) end_dig(g);
        else if (!strcmp(kind, "use")) end_use(g);
        else if (!strcmp(kind, "air")) end_air(g);
        else if (!strcmp(kind, "tower")) end_tower(g);
        else if (!strcmp(kind, "bed")) end_bed(g);
    }
    quit_row(g, g->t);
}

static const char *USAGE =
    "usage: playfuzz_ends SEED [--end KIND] [--use-slot K] [--throw-slot K] [--inv INV]\n"
    "    [--pose \"X Y Z YAW PITCH DIM\" --kit KIT.jsonl] [--ticks N] [--t0 2] > script.jsonl\n"
    "  playfuzz_ends --kinds | SEED --which\n";

static long long parse_ll(const char *s)
{
    char *e;
    long long v = strtoll(s, &e, 10);
    if (*e || e == s) die("playfuzz_ends: not an integer: %s", s);
    return v;
}

int main(int argc, char **argv)
{
    long long seed = 0, ticks = -1, t0 = 2;
    int have_seed = 0, which = 0, npose = 0;
    const char *kind = NULL, *kit_path = NULL;
    double pose[6];
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--ticks") && i + 1 < argc) ticks = parse_ll(argv[++i]);
        else if (!strcmp(a, "--t0") && i + 1 < argc) t0 = parse_ll(argv[++i]);
        else if (!strcmp(a, "--end") && i + 1 < argc) kind = argv[++i];
        else if ((!strcmp(a, "--use-slot") || !strcmp(a, "--throw-slot")) && i + 1 < argc) {
            long long v = parse_ll(argv[++i]);
            if (v < 1 || v > 9) {
                fprintf(stderr, "ends: %s takes 1 to 9\n", a);
                return 2;
            }
            if (!strcmp(a, "--use-slot")) use_slot = (int)v;
            else throw_slot = (int)v;
        } else if (!strcmp(a, "--inv") && i + 1 < argc) {
            inv.set = 1;
            for (int s = 0; s < 36; ++s) inv.id[s] = -1;
            char *t = xstrdup(argv[++i]);
            for (char *e = strtok(t, ","); e; e = strtok(NULL, ",")) {
                char *c1 = strchr(e, ':'), *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
                if (!c1 || !c2 || strchr(c2 + 1, ':')) die("playfuzz_ends: --inv wants SLOT:ID:COUNT,...");
                *c1 = *c2 = 0;
                long long sl = parse_ll(e), it = parse_ll(c1 + 1), n = parse_ll(c2 + 1);
                if (0 <= sl && sl < 36) inv.id[sl] = (int)it, inv.count[sl] = (int)n;
            }
            free(t);
        } else if (!strcmp(a, "--pose") && i + 1 < argc) npose = parse_pose(argv[++i], pose);
        else if (!strcmp(a, "--kit") && i + 1 < argc) kit_path = argv[++i];
        else if (!strcmp(a, "--which")) which = 1;
        else if (!strcmp(a, "--kinds")) {
            for (int k = 0; k < NENDS; ++k) printf("%s\n", ENDS[k].k);
            return 0;
        } else {
            seed = parse_ll(a);
            have_seed = 1;
        }
    }
    if (!have_seed) {
        fputs(USAGE, stderr);
        return 2;
    }
    if (ticks < 0) ticks = default_ticks(seed);
    if (!kind) {
        struct pyrand r;
        pr_seed_int(&r, seed * 104729 + 7);
        int tot = 0;
        for (int k = 0; k < NENDS; ++k) tot += ENDS[k].w;
        double x = pr_random(&r) * tot;
        for (int k = 0; k < NENDS; ++k) {
            x -= ENDS[k].w;
            if (x < 0) {
                kind = ENDS[k].k;
                break;
            }
        }
    }
    if (which) {
        printf("%s\n", kind ? kind : "None");
        return 0;
    }
    int known = 0;
    for (int k = 0; k < NENDS; ++k) known |= kind && !strcmp(kind, ENDS[k].k);
    if (!known) {
        fprintf(stderr, "ends: no end kind %s\n", kind ? kind : "None");
        return 2;
    }
    slot_points();
    struct kit kit;
    if (npose) kit_init(&kit, seed, pose);
    struct gen g;
    gen_init(&g, seed, ticks, t0, npose ? &kit : NULL);
    run_end(&g, kind, seed);
    write_rows(&g.out);
    fflush(stdout);
    if (kit_path && write_kit(kit_path, &g, &kit)) return 1;
    return 0;
}
