/* The vector env (lane/vecenv): libneth's envs as a PufferLib-style vector
 * env, everything a trainer did per env per step done in C on the env's
 * pool worker. What mcsr-speedrun's semteach.Loop did in Python each group
 * step (mcsr/rl/semteach.py, macros.py, reward.py, actmap.py) runs here:
 *
 *   before the step's first tick   the policy's action (actions[e]) to the
 *                                  act (actmap.to_acts: the camera bins to
 *                                  look deltas, the keys to holds and
 *                                  presses), then the five macros on top
 *                                  (macros.Macros.apply: surface, screen,
 *                                  loot, mine lock, craft)
 *   after the step                 the reward (reward.Reward.step), the
 *                                  episode's books (Loop.run, Loop.finish),
 *                                  the scalars (semteach.scalars), the clock
 *
 * The trainer's calls (one thread):
 *   neth_vec_make(opts)      N envs in G groups, every group's first step queued
 *   neth_vec_buffers(b)      the flat buffers it maps once (below)
 *   neth_vec_recv()          the next group whose envs are all done: its index
 *   neth_vec_send(g)         group g stepped with actions[g's envs]
 *   neth_vec_episodes(out)   the finished episodes' records since the last call
 *
 * A group's envs play their episodes in lockstep, as Loop's do: when its
 * episode clock reaches ep_len, recv records the episodes, resets the
 * group's envs to the start (on the workers) and steps them once with no
 * act, and returns the group then: its rewards and truncations are the
 * last step's, its observation and scalars the new episode's first.
 *
 * Every value is computed as numpy and CPython compute it (float32 where
 * numpy keeps float32, the pairwise sums' order, CPython's math.dist and
 * math.hypot), so the acts, rewards, scalars and episode records equal
 * Loop's (mcsr-speedrun scripts/vecenv_gate.py), log1p and sin within an
 * ulp. Linux and CUDA only, in libneth.so (rlbind.mk). */
#ifndef NETHERITE_RUNTIME_VECENV_H
#define NETHERITE_RUNTIME_VECENV_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "rlbind.h"

#define NETH_VEC_ACTION 16      /* pitch bin, yaw bin of the 41-bin mu-law camera, the 14 keys 0/1 */
#define NETH_VEC_CAM 41
#define NETH_VEC_KEYS 14
#define NETH_VEC_ITEMS 32       /* semteach.ITEMS */
#define NETH_VEC_MACROS 6       /* macros.MACRO_NAMES: none surface screen loot mine craft */
#define NETH_VEC_MILESTONES 13  /* reward.MILESTONES */
#define NETH_VEC_PICKS 20       /* lane/picks: 0 none, 1 + a craft plan (vecenv.c C_PLANKS .. C_TAKE, neth_vec_pick_name) */
#define NETH_VEC_NSCALAR (NETH_VEC_ITEMS + 9 + 12 + NETH_VEC_MACROS + 2 * NETH_VEC_CAM + NETH_VEC_KEYS)   /* 155 */

/* macros: the Macros switches (screen and the respawn are always on) */
enum { NETH_VEC_SURFACE = 1, NETH_VEC_LOOT = 2, NETH_VEC_MINE = 4, NETH_VEC_CRAFT = 8, NETH_VEC_ALL = 15 };
/* off by default (macros.py has none of it, lane/table2): the craft macro makes a new table for the stone pickaxe when
 * the placed one is 4 blocks or more away (macros._craft_plan crafts one table an episode, and the stone pickaxe only
 * beside it) */
enum { NETH_VEC_TABLE2 = 16 };
/* off by default (macros.py has none of it, lane/ironmacro): the craft macro's
 * iron plans: a furnace (8 cobblestone at a table) when there is iron ore and
 * no furnace at hand, smelting (the furnace placed and opened, the ore and its
 * fuel in, the ingots out when it is done), then an iron pickaxe, a bucket and
 * flint and steel at a table */
enum { NETH_VEC_IRON = 32 };
/* off by default (lane/latecraft): the craft macro's later recipes, a
 * diamond pickaxe at a table, blaze powder from every blaze rod and eyes of
 * ender from pearls and powder in the inventory's grid */
enum { NETH_VEC_LATE = 64 };
/* off by default (lane/liquidlook): the craft macro's bucket plans on the
 * liquid the look's ray (rlbind.h neth_rl_state lq_*) lands on, what the player
 * sees in front of them: an empty bucket filled at a water source, a water
 * bucket poured at a lava source (the lava sources it reaches turn to
 * obsidian); and the mine macro takes obsidian with a diamond pickaxe */
enum { NETH_VEC_LIQUID = 128 };
/* off by default (lane/portal): the craft macro's portal plan, holding 10
 * obsidian, flint and steel and 7 cobblestone in the overworld: a frame 2
 * ahead (the bottom row and the columns' first blocks from the ground, the
 * rest from a 3-high cobblestone tower jumped up in place, cobblestone in
 * the four corners), lit on the bottom obsidian, then walked into; the frame's
 * cells cleared on the way (grass placed over, anything else dug) */
enum { NETH_VEC_PORTAL = 256 };
/* off by default (lane/endframe): the craft macro's eye plan: the crosshair
 * on an end portal frame block without an eye, eyes of ender held: one put
 * in (the frame's twelfth lights the portal); and (lane/throweye) the
 * policy's throw key with eyes held in the overworld: an eye selected and
 * thrown where the look is (it flies toward the nearest stronghold, in view) */
enum { NETH_VEC_END = 512 };
/* off by default (lane/fight): the mine macro's fight: the policy's attack
 * with a hostile living under the crosshair locks on it (the mine lock's
 * kind: that step is the policy's), keeps it in the look while it is in
 * view (rlbind.h neth_rl_state vis: never through a wall), walks to it from
 * 2.6 to 10 blocks, takes the hotbar's best weapon and hits whenever the
 * crosshair is on it and the last hit is 10 ticks old (the target's hurt
 * time); over when it is out of view 10 steps, dead, or after 400 steps */
enum { NETH_VEC_FIGHT = 1024 };
/* off by default (lane/carry): the table carried, as speedrunners do: with a
 * pickaxe made and nothing left to make at the placed table this macro placed
 * or used (within 4 blocks), it is broken (an axe, else a hotbar slot with no
 * tool: a pickaxe would spend durability) and picked up, so the next 3x3 plan
 * places it from the inventory wherever the player is (a shaft included)
 * instead of finding none within reach; tried again 400 steps after a try */
enum { NETH_VEC_CARRY = 2048 };
/* off by default (lane/seek): ore the camera shows (rlbind.h neth_rl_state
 * seen, the observation's own ray cast: never behind a wall) and still
 * needed is gone to and mined, as a player who sees it does: iron ore with a
 * stone pickaxe while the iron pickaxe, the bucket and the flint and steel
 * want more than the ore and ingots held (3, 3, 1), diamond ore with an iron
 * pickaxe until 3 diamonds or a diamond pickaxe. The mine macro's lock on the
 * nearest such cell in view: the look on it, walking at it beyond reach, a
 * block the look meets first within reach dug out of the way, the ore dug
 * once the crosshair is on it; over when the item is held, and given up (that
 * cell left alone 1200 steps) on no half-block gain in 120 steps, the eye in
 * water, a liquid, bedrock or obsidian in the way, or after 600 steps */
enum { NETH_VEC_SEEK = 4096 };
/* the strip mine (lane/strip): with a stone pickaxe or better, iron still needed as NETH_VEC_SEEK counts it (or diamonds
 * with an iron pickaxe), none of it in view and nothing to craft, the player digs as a player hunting ore does: a 1x2
 * staircase down along the cardinal direction it faces to feet at y NETH_VEC_STRIP_Y, then a 1x2 tunnel straight on;
 * every cell judged by the crosshair (on it: dug with the best tool; through it: clear), the walk into the next cell
 * once both (three going down) are clear. Turned a quarter when the liquid look meets a source within 3 blocks, or
 * after 60 steps without a cell's progress; over after 300 steps (other macros start again), when the seek's ore is in
 * view, the want is met, or the eye is in water. Nothing it does reads a block the player does not see */
enum { NETH_VEC_STRIP = 8192, NETH_VEC_STRIP_Y = 12 };
/* the strip kept mining (lane/strip2, mods/macros/strip2.c; with NETH_VEC_STRIP, off by default): the strip, the seek and the
 * mine lock dig with the cheapest pickaxe that mines the block (stone, then wood, iron and diamond last), fetched from
 * the main inventory to the hotbar under the inventory screen when it ranks above the hotbar's best (a held iron
 * pickaxe outside the hotbar had left the strip punching stone by hand once the stone and wooden ones broke); a stone
 * pickaxe kept made beside an iron one while a table is at hand; the staircase's floor judged on the far part of its
 * top face (the centre's ray clipped the block under the feet: a quarter turn every level, a 2x2 spiral); a stair back
 * up to NETH_VEC_STRIP_Y from below; below y 16 nothing dug two or more under the feet; lava in view near (4 across,
 * one under to three over) turns the strip away from it, ahead (8 along, at the head or under) a quarter, on the liquid
 * ray within 3 back along its tunnel; the table's want 4 planks, none with an iron pickaxe held, a table made again for
 * the diamond pickaxe; under 3 free slots the surplus (cobblestone past 128, dirt, gravel, saplings) thrown behind */
enum { NETH_VEC_STRIP2 = 16777216 };
/* the screen macro off (lane/ladder10; a switch that turns a macro off, so every level's number without it is the same
 * macros as before and the macro stays on by default). The screen macro starts whenever a screen other than the death
 * screen is open and no macro is running (a table, furnace or chest the policy's use opened, the inventory a macro left
 * open), drops the cursor's stack outside the window (a click at -999) and closes it (the credits left by their
 * respawn with NETH_VEC_DRAGON); 0.30% of the teacher's steps at L3b. With this bit the screen stays open until the
 * policy closes it: under the 16-wide action that needs actions.gui cursor (the inventory key, key 12); the credits'
 * respawn stays on. Its label's mbit is NETH_VEC_LBL_SCREEN as before */
enum { NETH_VEC_NOSCREEN = 33554432 };
/* the craft macro's two halves (lane/ladder12), each a switch that turns its half off (on by default, as NOSCREEN), for
 * the crafting plans only (the 2x2 and 3x3 recipes and the smelt: vecenv.c plan_gui; the bucket, portal, frame, throw
 * and take plans are the craft macro's whole with either bit). The split is the macro's phase: CP_OPEN is the table
 * half, CP_CLICKS on the recipe half.
 * - NETH_VEC_NOTABLE turns off the table: getting the plan's window open (the inventory key for a 2x2 plan; for a 3x3
 *   plan the crafting table placed from the hotbar or fetched to it under the inventory screen, looked at and used;
 *   the furnace the same for the smelt). The craft macro then starts no crafting plan with no screen open; under an
 *   inventory, crafting or furnace window the policy opened, tidy (nothing on the cursor, the grid empty), it starts
 *   the plan craft_plan names where it fits the window (a 2x2 plan the inventory, a 3x3 one the table, the smelt the
 *   furnace) from its clicks, and remembers the table or furnace the crosshair shows (the one the policy opened).
 * - NETH_VEC_NORECIPE turns off the recipe: the clicks under the open window (the plan's parts, the result taken; the
 *   smelt's ore, fuel, wait and the ingots taken) and the close. The craft macro opens the window as before and gives
 *   the step to the policy the moment its clicks would start (that step the policy's).
 * Labels say the half (neth_vec_label.half), and the distiller counts a craft label in the level unless the level
 * turns its half off */
enum { NETH_VEC_NOTABLE = 67108864, NETH_VEC_NORECIPE = 134217728 };
/* the recipe as the policy's one pick (lane/ladder12, with NETH_VEC_NORECIPE; off by default): under a tidy inventory,
 * crafting or furnace window where the plan craft_plan names fits, the policy's jump key (key 5, which moves nothing
 * under a screen) starts the recipe half on that plan, the brief's 3x3 recipe pick with one choice (the native net has
 * no pick head): the clicks, the result and the close are the macro's again; on the step the craft macro would give
 * the window to the policy, a held jump goes on with the clicks. Its label: the recipe half's first step (half 3) is
 * the jump key, the rest of the recipe the level's own macro */
enum { NETH_VEC_RECIPEKEY = 268435456 };
/* the table half as the policy's one key (lane/ladder15, with NETH_VEC_NOTABLE; off by default): with no screen open
 * and craft_plan naming a crafting plan (vecenv.c plan_gui: a 2x2 or 3x3 recipe or the smelt), the policy's inventory
 * key (key 12, actions.gui cursor) starts the craft macro's table half on that plan at once (not on the macro's
 * five-step cadence): the crafting table or furnace placed or fetched, looked at and used, or for a 2x2 plan the
 * inventory key pressed, as without NETH_VEC_NOTABLE. Key 12 because in those states it has no other use: it opens the
 * inventory, which for a 2x2 plan is the table half's own act and for a 3x3 plan or the smelt a window the plan does
 * not fit; and it is the key the student already says for every window it opens (press_label key). The key is read on
 * the start's step only (the level, not the edge: a key held from the step before starts it too): the table half runs
 * to its window without it, so no step after the first asks the key held (RECIPEKEY's hand-off at every call is what
 * recipe_hold had to teach). Its label: the table half's first step (half 4) is key 12 alone, no look; its later steps
 * the level's own macro. With no crafting plan named, key 12 is the policy's (the inventory opened on its edge) */
enum { NETH_VEC_TABLEKEY = 536870912 };
/* actions.table_key paused (vecenv.c MOPTS): the table key is key 13 (the action's last, "paused", read by nothing
 * else) instead of key 12 */
enum { NETH_VEC_KEY_PAUSED = 13 };
/* the seek as the policy's one key (lane/ladder16, with NETH_VEC_SEEK off; off by default; the int32 macros' last free
 * bit): where the seek's own start holds (vecenv.c seek_want: iron ore in view with a stone pickaxe and iron still
 * needed, diamond ore with an iron pickaxe, a log when a craft waits on wood; the nearest such cell not given up on), the
 * policy's key 13 (NETH_VEC_KEY_PAUSED, the action's last key, which nothing else reads: key 12 is NETH_VEC_TABLEKEY's
 * and the inventory's, and ladder15 measured a key with a second use in the same states at 1.36 pooled against 2.86 on
 * key 13) starts the seek on that cell on any step (the macro's own start is every fifth): the walk, the digs in the
 * way and the ore's dig are the seek's as with NETH_VEC_SEEK, and it ends as the seek ends (the item picked up, or the
 * cell given up). The key is read on the start's step only (its level), so no later step asks it held. Its label: the
 * seek's first step (half 6) is key 13 alone, no look; its later steps the level's own macro. Key 13 is also
 * actions.table_key paused's: do not use the two together */
enum { NETH_VEC_SEEKKEY = 1073741824 };
/* the bits that turn a macro off or change one (passed to the level's copy as they are) */
#define NETH_VEC_OFF_BITS                                                                                              \
    (NETH_VEC_NOSCREEN | NETH_VEC_NOTABLE | NETH_VEC_NORECIPE | NETH_VEC_RECIPEKEY | NETH_VEC_TABLEKEY |              \
     NETH_VEC_SEEKKEY)
/* the food macro (lane/food): the pig, cow, mooshroom and chicken the client's view holds within 48 blocks, ranked
 * after every hostile, hunted for meat (a wooden pickaxe or better, under 6 meat held), and a meat eaten at food 10
 * or under, or at 17 or under with health missing; the hunt walks to the animal, kills it, and walks to its drop */
enum { NETH_VEC_FOOD = 16384 };
/* the pickaxe replaced (lane/pick): with no pickaxe held, or a wooden or stone one, the wood the next one needs
 * (7 planks, 9 when its sticks must be made too, 3 with a table held or in reach) is kept up from a log in view,
 * and the table and the pickaxe are made again from it when the held one breaks */
enum { NETH_VEC_PICK = 32768 };
/* the policy's own hotbar (lane/nthotbar; off by default, a switch of macros' bits only so that neth_vec_opts keeps
 * mcsr-speedrun's ctypes layout): the action is NETH_VEC_ACTION + 1 wide, its last element 0 to keep the slot or k (1
 * to 9) to press the number key k, slot k - 1, that tick, on a step the policy drives (a step a macro drives keeps the
 * macro's own slot choices); net.h's NETH_NET_HOTBAR head writes it. bufs.actions is then [n][NETH_VEC_ACTION + 1] */
enum { NETH_VEC_HOTBAR = 65536 };
#define NETH_VEC_HOT 10         /* NETH_VEC_HOTBAR's last element: 0 keep, 1 .. 9 */
/* the End fight (lane/dragon, vecdragon.c; off by default): in the End, alive, a sword or a bow with arrows held,
 * the macro holds the step to the exit portal: up out of the arrival's hole (a 1x1 dig and a cobblestone tower), the
 * ender crystals in view shot with the bow (each one's arc solved from EntityArrow's own flight), the island's centre
 * walked toward to find more, then the dragon fought with the sword whenever it is in reach (its head in the look),
 * endermen never looked at, food eaten at need, and once its death is seen the exit portal under where it died
 * walked into; the credits left by their respawn. Over when the player leaves the End or dies */
enum { NETH_VEC_DRAGON = 2097152 };
/* lane/dragon: the milestones past reward.MILESTONES as bits of the episode record's ms and of the save mask
 * (save_item -1): bit NETH_VEC_MILESTONES the Nether (the mask's only; the record has it in dims), then the End
 * entered and the dragon killed (its death seen: the boss's health at 0) */
enum { NETH_VEC_MS_END = NETH_VEC_MILESTONES + 1, NETH_VEC_MS_DRAGON = NETH_VEC_MILESTONES + 2,
       NETH_VEC_MS_BITS = NETH_VEC_MILESTONES + 3 };
/* the Nether (lane/nether, mods/macros/nether.c; off by default): from just through the portal to back in the overworld
 * with 7 blaze rods. In the Nether, alive, no screen open (after the screen, the lava escape and eating): the portal
 * cell stood in on arrival remembered; a look round, then a heading held along z, walked cell by cell as the strip
 * walks (the head and feet cleared, the floor judged first, a gap bridged with cobblestone, a quarter turn at lava or
 * a drop); nether brick or a fence in view gone to; the blaze spawner in view (netherview.h: never through a wall)
 * gone to and camped at; blazes in view fought with NETH_VEC_FIGHT's lock (pigmen never hit), each kill's drop walked
 * to; with 7 rods the walk back to the portal cell and into it. Given back in the overworld. Also the craft macro's
 * sword plan (overworld, a stone pickaxe made, no sword as good): an iron sword from 2 spare ingots, else a stone
 * sword; eating in the Nether (NETH_VEC_FOOD); the rods kept from NETH_VEC_LATE's powder in the Nether. Its steps
 * are labelled with its own bit (mbit_of) */
enum { NETH_VEC_NETHER = 262144 };
/* the stronghold (lane/stronghold, mods/macros/stronghold.c; off by default): from the overworld holding 12 eyes of ender
 * (then on while eyes are held there) to the End: an eye thrown and watched in flight (the eye in view, shview.h),
 * its drop picked up; two throws 24 blocks apart crossed for the target, walked to, thrown again there; an eye that
 * flies down (within 12 blocks of the portal room) names the room's column from its flight, dug down to by a
 * staircase round it until the room's stone brick shows, then the room's roof broken over the frames; the frames
 * filled from the top of the frame row, the lit portal walked into. Its steps are the mine macro's kind; it gives the
 * step back for eating, every SH_YIELD steps, and for good in the End or with no eyes left */
enum { NETH_VEC_STRONGHOLD = 1048576 };
/* the ender pearls (lane/pearl, mods/macros/pearl.c): in the overworld at night (the day's time 13000 to 23000, the sky the
 * screen shows) while pearls and eyes held are under 12, endermen hunted as a speedrunner does: a 3-high cobblestone
 * pillar stood on (a mob's melee needs its feet within 2 blocks of the player's and the boxes level, a creeper's fuse 3:
 * none reaches the top from the ground), the view turned to find one (rlbind.h neth_rl_far: in view out to 64 blocks,
 * never through a wall), the box middle stared at until it screams (provoked, the vanilla way), then it is watched by
 * its top (the box middle in the look within 4 blocks teleports it away) and hit with the hotbar's best weapon when
 * the crosshair is on it; once the kill shows, the pillar is dug down and the drop walked to. With none in view a
 * scan from the pillar, then a walk on (a quarter turn when stuck or in water) and a pillar again; on the ground,
 * hostiles within 10 blocks fought with NETH_VEC_FIGHT's lock and creepers within 7 fled. Given back by day, at 12
 * pearls and eyes, to eating (NETH_VEC_FOOD) and to death; the pillar is dug down first, so it leaves the cobblestone
 * held. lane/pearl2: the scan's pillar is 5 high (dug down to 3 for a provoked one within 8 blocks), the walk steers
 * toward the frame's longest dry view (rlbind.h neth_rl_far depth), a short view cuts a scan, a hit from out of view is
 * turned to (and a pillar left when no enderman is about), at 10 HP or under a hostile near is fled from, at 8 HP or
 * under with nothing near (feet at y 66 or over) the hide: a 3-deep hole sealed with one cobblestone, held to 18 HP,
 * mined open and climbed out; a kill is the stats screen's endermen killed. Its end and what it leaves held are as
 * above (the hide climbs out first: night's end comes out of the hole). Off by default. lane/pearlgate: the hunt is
 * the chain's pearl step, not any night: run-file option pearl_when (default nether: a blaze rod or blaze powder also
 * held; night is lane/pearl's; powder, the blaze held covering the 12 eyes), and pearl_yield (default on: not begun
 * while a bucket portal is cast, the stronghold searched, the strip climbs out, within 100 steps of a fight, or with
 * the late macro's blaze powder or eye to craft) */
enum { NETH_VEC_PEARL = 524288 };
/* lane/pearl3: the pearl step's cave hunt (mods/macros/pearl.c PP_STAIR, PP_TUNNEL, PP_HOLD; with NETH_VEC_PEARL; off by
 * default). pearl2's census put the endermen within 64 blocks in caves (42 to 93% more than 6 under the eye), so the
 * step is wanted by day too (pearls and eyes under 12, the overworld) and goes to them: a staircase down to feet at
 * y PEARL_CAVE_Y, then a 1x2 tunnel (every cell judged by the crosshair, as the strip mine's); where the frame's depth
 * shows a long view (a cave the tunnel opened), the player backs into the tunnel PEARL_CAVE_BACK cells and holds it,
 * looking out along the long view: an enderman (2.9 high) cannot enter a 2-high tunnel, and from 2.5 blocks in its
 * attack (feet within 2.0) does not reach while the player's does. One in view is stared at until it screams and hit
 * when the crosshair is on it; its drop is fetched once quiet, then the hold is gone back to; a quiet hold of
 * PEARL_CAVE_HOLD steps is left by a branch a quarter turn on. Given back as the pearl step is (at 12, to eating, to
 * death) but not at dawn. lane/brutall: all of it only with a pickaxe held (mods/macros/pearl.c PEARL_CAVE_TIER, wood or
 * better); without one the hunt is the night's surface one and by day not wanted (from spawn with nothing held the
 * staircase was dug by hand every step: brutetab's `all` row read 0 logs) */
enum { NETH_VEC_PEARLCAVE = 4194304 };
/* the portal's obsidian (lane/obsidian, mods/macros/obsidian.c; off by default): with the portal still wanted (the
 * overworld, never the Nether this episode, under 10 obsidian, flint and steel, a diamond pickaxe and a bucket held),
 * the obsidian, water sources and lava sources the eye sees (obsview.h, never behind a wall) gone to: obsidian dug, an
 * empty bucket filled, a water bucket poured on lava; the strip mine searches when none is in view; burning with a
 * water bucket, it is poured at the feet. Its steps are labelled with this switch (neth_vec_label's mbit).
 * lane/obsidian2: the search's strip only with the feet at y 15 or above (mods/macros/obsidian.c OB_SEARCH_Y; below it a
 * staircase climb), turned by lava in view ahead at the head's level or below and reversed after a lava touch; a look
 * around (twelve looks) every 10 blocks before the strip; water and lava sources seen remembered and gone to; a cast
 * poured from the bucket's reach, with a staircase down toward lava below it */
enum { NETH_VEC_OBSIDIAN = 131072 };
/* the bucket portal (lane/obsidian3, mods/macros/bportal.c; off by default): no diamond pickaxe, nothing mined. With flint
 * and steel and two buckets held in the overworld (the Nether not yet entered), the water and lava sources the eye sees
 * remembered; a bucket filled at the nearest water, the other at the nearest lava; beside that lava a slot dug into the
 * ground one cell ahead, its walls the mould, and the frame cast in it cell by cell (a water source put beside the
 * cell, lava poured into it: obsidian where it lands; the water picked back up), the lava fetched a bucket at a time;
 * two blocks under the top row while it is cast, dug out; lit with flint and steel and walked into. The craft macro's
 * IRON plans make the second bucket. Its steps are labelled with this switch */
enum { NETH_VEC_BPORTAL = 8388608 };     /* 4194304 was lane/pearl3's NETH_VEC_PEARLCAVE first */
/* reward.OBJECTIVES */
enum { NETH_VEC_FULL, NETH_VEC_LOGS, NETH_VEC_LOOTOBJ, NETH_VEC_CHESTS };

struct neth_vec_opts {
    struct neth_rl_opts env;       /* the binding's (rlbind.h) */
    int32_t groups;             /* 0: 2; n must divide by it */
    int32_t ep_len;             /* the policy's steps an episode (0: 3600, 180 s at 20 Hz) */
    int32_t macros;             /* NETH_VEC_* bits (Loop's macros=True: NETH_VEC_ALL) */
    int32_t objective;          /* NETH_VEC_FULL .. */
    int32_t ring;               /* episode records kept until read (0: 65536) */
    int32_t flags;              /* neth_rl_make_ex's (rlgui.h NETH_RL_DEV: a dev start, the gate's scenarios) */
    /* the tables Python computes, so both sides use the same numbers (NULL:
     * neth_vec_tables' own): reward.values() and pairs.mulaw_center of the
     * 41 bins */
    const float *values;        /* [4096] */
    const double *camdeg;       /* [41] */
    /* a reward of the trainer's own (lane/rewards), each off at NULL or 0 so
     * the objective stays reward.OBJECTIVES': weights replaces the
     * objective's row (items, looted, logs, chest, progress, ach, death,
     * milestone); ms_weights counts each new milestone (reward.MILESTONES'
     * order) at its weight in the milestone term instead of 1; log_cap > 0
     * pays the logs term and the held logs' value for the first log_cap logs
     * only */
    const double *weights;      /* [8] */
    const double *ms_weights;   /* [NETH_VEC_MILESTONES] */
    int32_t log_cap;
    /* item_caps (NULL: none, lane/itemcap): the items term counts at most
     * item_caps[id] of an item id over the 36 slots (0: all of it), so no item
     * pays past its cap however many are held */
    const int32_t *item_caps;   /* [4096] */
    /* a mix of starts (lane/mixstart; nstarts 0: the make's start only):
     * starts[k] a snapshot directory loaded as start k + 1 (start 0 the
     * make's), start_weights[nstarts + 1] each start's share; every env draws
     * its start at each reset, its first episode's too, and its episode
     * record names it */
    const char *const *starts;
    const double *start_weights;
    int32_t nstarts;
    /* starts saved from the envs' own episodes (lane/ownstarts; save_item 0:
     * off): an env holding save_count or more of item save_item is saved once
     * an episode (rlbind.h neth_rl_start_save, the library made with NETH_RL_SAVE),
     * up to save_pool kept (the oldest no env plays from let go, else the
     * new one; at most what the envs and starts leave of the images' address
     * window, image.h IMG_SLOTS: 40 at 1024 envs and three starts), and each reset takes one of them at
     * random with share save_share once there is one (the mix otherwise); its
     * episode record's start is nstarts + 1. save_item -1 (lane/msstarts):
     * saved at each milestone of the mask save_count (bit i reward.MILESTONES[i],
     * bit NETH_VEC_MILESTONES the Nether) an episode gains past what it began
     * with; each save's level the mask's milestones then held, the draw a
     * level present at random and one of its starts, a full pool letting go
     * the oldest unplayed start of its most kept level; the record's level */
    int32_t save_item, save_count, save_pool;
    double save_share;
    /* lane/netherw: paid once an episode at its first step in the Nether (0: off) */
    double nether_weight;
    /* lane/explore (API 7; 0: off): paid at each step that enters a 4x4x4 block cell the episode has not been in, in
     * the overworld below y 56 (under the surface, where iron is), the eye out of water (lane/explore2: the sea
     * floor near the spawn lies below 56, and swimming down there is not digging), for the first explore_cap such cells an episode
     * (at most NETH_VEC_EXPLORE_MAX; the episode record's cells counts them either way). From spawn the policy stayed within 11 blocks of where it began in 90% of
     * 20-minute runs, digging one shaft, and found iron only where a vein met it */
    double explore_weight;
    int32_t explore_cap;
    /* lane/picks (API 10; 0: off, the craft macro starts on the state as before): 1, the craft macro's plans are the
     * policy's to choose. Each step the trainer writes a pick to bufs.picks (0 none, 1 + a plan), the plan runs when
     * bufs.mask allowed it (its parts held, its table or furnace at hand: the macro could make it now) and no other
     * macro starts first, and that step counts as the policy's (bufs.chosen 1, driver 0); the craft macro never starts
     * on its own */
    int32_t picks;
    /* lane/seedwin (API 11; 0: every start of the mix loaded at the make, as before): a mix of more starts than
     * memory holds (a seed's start is about 50 MB resident at train-fast: 99 beside 32 envs peaked at 6.9 GB, 2.5 GB
     * with at most 16 of them loaded; 1,000 seeds' would be about 50 GB). A group's
     * r-th reset (its round; the make's draws round 0) draws from start_window of the mix's starts, starts[(r *
     * stride + j) mod nstarts] for j < start_window, stride start_stride (0: half the window, at least 1), so the
     * window moves through every start in the order given. Only the rounds in flight have their starts loaded:
     * the next round's load in the background on the workers, and a start no round in flight draws is freed once no
     * env plays from it (about start_window + 2 stride loaded). The make's start keeps its share (start_weights[0]);
     * the mix's starts are weighted equally (other weights are refused with a window). An env's draws depend on
     * its own state and its group's round only, so the trajectories are the same at any workers and grouping. The
     * episode record's start is the mix's index as before (1 + k for starts[k]) */
    int32_t start_window, start_stride;
    /* lane/dragon (API 12; 0: off): paid once an episode at its first step in the End, and at the step its dragon's
     * death is seen */
    double end_weight, dragon_weight;
    /* lane/starts (API 13; 0: off, the 155 scalars as before): 1, NETH_VEC_LATE_N more after them, the late game the
     * 32 items leave out (vecenv.c LATE_ITEMS: log1p of each count held, then the Nether and the End as 0 or 1), so a
     * policy started mid-game sees what it holds; bufs.scalars is then [n][NETH_VEC_NSCALAR + NETH_VEC_LATE_N].
     * lane/ladder2 + lane/hill-rnn-hotbar: bit 1 (2), NETH_VEC_HSEE_N (108 = 9x12) more after those, what the 1.7.10
     * HUD hotbar shows per slot 0..8: pickaxe/axe/shovel/sword tier over 4, bow, snowball (log1p(min(c,16))/log1p(16)),
     * arrow/building-block/edible-food (log1p(min(c,64))/log1p(64)), water bucket, empty bucket, and flint & steel, so
     * a raw-action policy that picks its own slot (NETH_VEC_HOTBAR) sees which number key holds each tool, weapon,
     * projectile, block, food, or utility item; the 32 counts say what is held, not where. Bit 0 alone is
     * lane/starts' 1 as before. lane/ladder4: bit 2 (4), NETH_VEC_DIG_N more after those, the dig the screen shows:
     * the crack stage the client draws on the block under the crosshair ((stage + 1) / 10, 0 none: RenderGlobal's
     * destroy-progress overlay, PlayerControllerMP.curBlockDamageMP * 10 - 1) and 1 while the client hits a block
     * (isHittingBlock: the hit particles and the arm's swing), so a policy can tell a dig under way from any other
     * step with the attack held (in 1.7.10 one tick without attack drops the block's damage to 0).
     * lane/ladder5: bit 3 (8), NETH_VEC_ORE_N more after those, what NETH_VEC_SEEK reads of the view (neth_rl_state
     * seen: the observation's own ray cast, never behind a wall): for iron ore, diamond ore and logs, 1 when a face of
     * it is drawn, then the nearest such cell's yaw from the look over 180, its pitch from the look over 90 and its
     * distance from the eye over 16 (all 0 when none is drawn), so a policy without the seek can be told where the
     * ore it sees is.
     * lane/ladder10: bit 4 (16), NETH_VEC_GV_N more after those, what the open container screen shows (all 0 with no
     * screen open): the screen's kind (inventory, crafting table, furnace, any other) as 4 flags, then for each of
     * the cursor grid's 63 cells (vecenv.h NETH_VEC_GUI_*: the screen's own slots, the main inventory, the hotbar) and
     * the stack on the cursor, the stack's count over 64 at its class of NETH_VEC_GV_K (log, planks, stick,
     * cobblestone, coal, iron ore, iron ingot, diamond, crafting table, flint, any other item): a player looking at
     * the window sees which slot holds what, which the 32 counts do not say */
    int32_t late_scalars;
    /* lane/curric2 (0: off, the items term on everything held as before): 1, the items term pays only for what an
     * episode holds past what its start gave it: at the episode's first step each item id's count held is its
     * zero, and each slot's count is paid less what is left of that zero (slot order), so a start's kit is worth
     * nothing to keep and costs nothing to lose (a death costs the death term and what the episode itself gained).
     * A spawn start holds nothing: the reward is the same there */
    int32_t reward_rel;
    /* lane/macroopts (API 14; NULL or "": every option its default, today's behaviour): the macros' either/or
     * choices (Elliot, 2026-11-02: where a macro could work one way or another, each way is a run-file option, the
     * easiest the default), "name=word,name=word": the names and their words are vecenv.c's MOPTS table
     * (neth_vec_mopt_list prints it), the run file's policy.actions.NAME keys. An unknown name or word is refused by
     * neth_vec_make */
    const char *macro_opts;
    /* lane/stagelen (API 15; NULL: every episode ep_len long, as before): start_ep_len[nstarts + 1], each start's
     * episode length in ticks (0: ep_len; a saved start's is ep_len), taken at each reset that draws the start, the
     * make's included. A group's envs then end apart: recv resets the envs whose clock ran out alone (their reset
     * step a step of those envs only, the group's others held) and gives the group back once they are reset, so a
     * receive still has every env of the group stepped once, a reset env's reward and truncation its ended
     * episode's last step's. Refused with a start window (its rounds are the group's resets) */
    const int32_t *start_ep_len;
    /* lane/stagelen (0: o[7] the time played, te / 6000, as before): 1, o[7] the share of the episode's last five
     * minutes gone, max(0, 6000 - (ep_len - te)) / 6000 with the env's own ep_len: the same as 0 at a 5-minute
     * episode, 0 through a longer one's first part, so the value function reads the time left at every length */
    int32_t clock;
    /* lane/rawskill (task_item 0: off, everything as before): one physical skill on the raw controls, an episode one
     * attempt (vecenv_task.c). The episode ends at the first step the inventory holds more of item task_item than at
     * its first step (the success, paid task_win) or at a death (paid task_death), else at its clock; the objective's
     * terms are not paid (the task's are the whole reward). The shaping, each 0 off: task_look once an episode, at
     * the first step the crosshair is on a block task_block (0: task_item); task_crack for each crack stage the client
     * draws on that block past the episode's highest so far (10 stages); task_dist for each block the eye's distance
     * to the nearest task_block cell the cast shows (neth_rl_state seen: iron ore's, NETH_RL_SEEN 0, or a log's) comes under
     * the episode's least so far. task_keys: the keys the policy may press (bit j the action's key j; 0 all), every
     * other key up whatever the action says. task_shape 1 (0: each term paid once a unit as above) makes the three
     * terms one potential, paid as its change each step: -task_look * the look's angle to the last seen cell's middle
     * over 90 degrees (at most 1), -task_dist * the eye's distance to that cell, task_crack * (the crack stage drawn on
     * the block under the crosshair + 1, 0 none): a dig let go gives its stages back */
    int32_t task_item, task_block, task_keys;
    double task_win, task_death, task_look, task_crack, task_dist;
    int32_t task_shape;
    /* lane/rawskill (0 off): 1, an attack the policy presses with the crosshair on a block is held every later step
     * while the crosshair stays on that cell (vecenv_task.c task_keys), so a dig needs one press, not ~23 steps of
     * the key in a row (iron ore with a stone pickaxe); the act the engine steps holds it, so the episode replays.
     * 2: the same, latched only on a block task_block (any other block's dig is the policy's own) */
    int32_t task_sticky;
    /* lane/rawskill (0 off: every reset draws its start): 1, each env keeps the start its first draw gave it (the
     * make's draw, as before), so every reset is to the start the env played from:
     * the pool's delta path (a 26-start library at 50-step episodes: 12 to 17 ms a reset against 36 to 91 ms when
     * the draw switches start). The mix's weights hold across the envs, not over one env's episodes. Not with a start
     * window, the saved starts' pool or the curriculum (each moves the draw) */
    int32_t start_pin;
    /* lane/eval1k (0 off): 1, env i's draws are not random: its j-th episode plays mix start 1 + (i + j * n) mod
     * nstarts (the library's starts in index order, start 0 never), so an eval of n envs plays every start the same
     * number of times (a multiple of them n / nstarts apiece when nstarts divides n) and the start of an env is a
     * function of its index alone, the same for every model. Not with a start window */
    int32_t start_cycle;
    /* lane/rawsearch (0 off): the task's privileged hint, a teacher's: a step whose cast shows none of the task's
     * block (a log, iron ore) has the late scalars' seen-object slot of that block (late_scalars bit 3, the .or
     * builds) filled from the nearest such block in a box task_hint blocks out in x and z around the eye (rlscan.h:
     * the client world, what the server sent), as if it were seen, and the potential's cell (task_shape) is that
     * block. A student trained from such a teacher has those scalars zeroed (ntloop --student-zero ore). -1 (with
     * task_shape 1): memory, not the world: the slot holds the cell of the task's block last drawn this episode (the
     * potential's cell), so a teacher that turned away still knows where the block it saw is; no scan */
    int32_t task_hint;
    /* lane/rawsearch (0 or 1: the first pickup ends the episode, as before): N, the episode ends at the first step
     * the inventory holds N more of task_item than at its first step, each one more paid task_win / N (several logs
     * from one tree: the trunk's blocks above the first, looked up at) */
    int32_t task_count;
    /* lane/dbench (0 off, today's behaviour): 1, each env keeps the dragon benchmark's books of its episode (struct
     * neth_vec_db, read with neth_vec_db between recv and send): the step each milestone was first held, the steps by
     * the chain stage the run was at and the macro that drove them, and the deaths' places, drivers and falls. Counting
     * only: no act, reward or row changes */
    int32_t dbench;
    /* lane/acrl (0: as before): 1, task_hint fills the seen-object slot only (the privileged critic's input, ntloop
     * --critic) and the potential's cell stays the last one drawn, so the reward is the one without the hint */
    int32_t task_hint_noshape;
    /* lane/acrl (task_item2 0: off): a second task in the same run, task_item's rule with item task_item2 (its block
     * and seen slot), played by the envs whose episode starts from start task_from2 or later (the episode record's
     * start: 0 the make's, 1 + k the mix's starts[k]); late_scalars bit 5 (32) adds NETH_VEC_TASK_N scalars after the
     * others, the env's task as a one-hot (task_item's, task_item2's) the policy reads, as a person is told the goal.
     * Not with task_hint or task_count */
    int32_t task_item2, task_from2;
    /* lane/chain (0 off: everything as before): 1, the checkpointed search's hooks (train/chain.c). The library is made
     * with NETH_RL_SAVE; each env's last result is kept (neth_vec_chain_peek); neth_vec_chain_save saves env id now with
     * the vector env's own memory of its episode (the placed table and furnace, the milestones and the dimensions it
     * had: carried into every episode played from that start, as the archive's starts carry theirs); env id's next
     * episode's start and length are the caller's (neth_vec_chain_next); its episode can be ended at its next step
     * (neth_vec_chain_end); and its next decisions can be decoded with no macro (neth_vec_chain_free: the sampled
     * driver alone, so envs a macro would drive the same way from one start part). Nothing changes until one is
     * called */
    int32_t chain;
    /* lane/speedrun-recipe (each 0: off, every reward as before): step_pen is taken from each step's reward (a step's
     * cost, so the same milestones sooner pay more); stall_pen is taken from each step of a stall at least
     * stall_ticks steps long (0: 400). A stall is a run of steps whose feet stay within stall_radius blocks (0: 2) of
     * the run's first step's, in one dimension, with no milestone, dimension or rise of the held items' value (the
     * items term's) since it began; a death, a move out of the radius or any such progress begins a new one. The
     * stall's books (neth_vec_db stall_*, dbench) are kept with the penalty off too, at the same threshold and
     * radius */
    double step_pen, stall_pen, stall_radius;
    int32_t stall_ticks;
    /* opt-in dense dragon HP shaping (0: off, exact parity): paid per HP of strict new low in e->dg.dragon_min */
    double dragon_hp_weight;
    /* opt-in strict-new-low End crystal shaping (0: off, exact parity): paid once per newly destroyed server End crystal */
    double crystal_weight;
};
#define NETH_VEC_TASK_N 2      /* lane/acrl: late_scalars' bit 5, the task one-hot */
/* lane/macroopts: a macro option's word checked: its index (the value the macro reads), -1 with err on an unknown name
 * or word; neth_vec_mopt_list writes every option, its words and its default, a line each */
int neth_vec_mopt_check(const char *name, const char *word, char *err, int en);
void neth_vec_mopt_list(FILE *f);
#define NETH_VEC_LATE_N 14
#define NETH_VEC_HSEE_N 108     /* lane/ladder2 + lane/hill-rnn-hotbar: late_scalars' bit 1, 9x12 legal HUD hotbar features (vecenv.c scalars) */
#define NETH_VEC_DIG_N 2        /* lane/ladder4: late_scalars' bit 2, the crack stage and the hit (vecenv.c scalars) */
#define NETH_VEC_ORE_N 12       /* lane/ladder5: late_scalars' bit 3, the seek's view of iron ore, diamond ore and logs
                                 * (vecenv.c scalars) */
/* lane/ladder10: the cursor grid of actions.gui cursor (vecenv.c gui_cell, gui_slot) and late_scalars' bit 4. Under a
 * container screen (the inventory, a crafting table, a furnace, a chest) the camera's two bins are the mouse's
 * position in the window, row NETH_VEC_GUI_B0 + r of the pitch bins and column NETH_VEC_GUI_B0 + c of the yaw bins:
 * rows 0 to 2 the screen's own slots (the crafting grid's 3x3 or 2x2 at columns 0 to 2, its result at row 1 column 4,
 * the inventory's armour at row 2 columns 5 to 8, the furnace's input at 0,0, its fuel at 2,0, its output at 1,4, a
 * small chest's 27 slots across all nine columns), rows 3 to 5 the main inventory, row 6 the hotbar, and row 7 column
 * 0 outside the window (slot -999: the cursor's stack thrown) */
#define NETH_VEC_GUI_B0 16
#define NETH_VEC_GUI_ROWS 8
#define NETH_VEC_GUI_COLS 9
#define NETH_VEC_GUI_CELLS 63    /* rows 0 to 6 */
#define NETH_VEC_GV_K 11
#define NETH_VEC_GV_N (4 + (NETH_VEC_GUI_CELLS + 1) * NETH_VEC_GV_K)   /* 708 */
enum { NETH_VEC_KEY_INV = 12 }; /* actions.gui cursor: the action's key 12 (swap_item, unused before) is the inventory
                                 * key: it opens the inventory with no screen open and closes a container screen */
/* lane/ladder10 (actions.gui cursor): the GUI books since the make, summed over the envs: [0] a macro's steps with a GUI
 * op or the inventory key, [1] of them the label says (NETH_VEC_LBL_GUI clear), [2] of those the label decoded as a
 * policy's action (the same state) makes the same op, [3] makes another (a bug), [4] the policy's steps under a
 * container screen, [5] its clicks, [6] its closes, [7] its inventory presses. Returns the gui option (0 off: all 0).
 * lane/ladder13, the recipe half's books (any gui option's craft macro; 0 without NETH_VEC_NORECIPE's hand-offs): [8]
 * the recipe's first steps of a 2x2 plan, [9] of a 3x3 plan, [10] of the smelt; NETH_VEC_NORECIPE's hand-offs [11] at
 * the recipe's start, [12] after it started (a step without the jump, NETH_VEC_RECIPEKEY) in a 2x2 or 3x3 plan, [13] in
 * the smelt's clicks or wait; [14] the smelt's ingots taken by the macro, [15] the policy's steps under a furnace; [16]
 * a crafting table's window opened (seen open after a step without it, whoever opened it), [17] a furnace's (gui cursor).
 * lane/ladder14 (gui cursor): the policy's steps under a crafting table's window [18..22] and the inventory's [23..27]
 * by what NETH_VEC_NOTABLE's recipe start (vecenv.c recipe_start) finds there: the window untidy (the cursor or the
 * grid holding a stack), craft_plan naming no plan, a plan that fits the window (the start handed back: no jump), a
 * window plan of another window (a 2x2 plan under the table, a 3x3 one or the smelt under the inventory), a plan with
 * no window (the bucket, the take); [28] [29] [30] of the crafting table's other-window steps, the plan planks, the
 * table, sticks; the policy's steps under a container screen with [31] the inventory key (key 12) held, [32] of them
 * held the step before too (no edge: no close), [33] the jump held.
 * lane/ladder15 (NETH_VEC_TABLEKEY): the table half started by the policy's key [34] for a 2x2 plan, [35] a 3x3 plan,
 * [36] the smelt; the policy's steps with no screen open while craft_plan names a crafting plan [37] (the key's chance),
 * [38] of them a 3x3 plan or the smelt.
 * lane/ladder16 (NETH_VEC_SEEKKEY; counted with any gui option): the seek started by the policy's key [39] on iron ore,
 * [40] diamond ore, [41] a log; the key's seek ended [42] with the item picked up, [43] dug and not picked up in 60
 * steps, [44] given up (no half-block gain in 120 steps, 600 steps, the eye in water, a liquid, bedrock or obsidian in
 * the way, a dig under the feet, a screen open), [45] by a death or the lava escape; [46] the steps the key's seek
 * drove; the policy's steps (no macro) [47] with the seek's start holding (the key's chance), [48] of them on iron ore,
 * [49] with key 13 down and no start holding (presses that start nothing).
 * lane/ladder18 (actions.seek_walk, seek_dig; [51] [52] at any macros): [50] key 13 down with the seek's start holding
 * where its half is the policy's (no start); [51] seek steps in the walk half, [52] in the dig half (the census, any
 * seek); [53] the key's seek ended as its cell left the macro's half; [54] the policy's steps with the start holding in
 * its own half (the walk's chance under seek_walk policy).
 * lane/ladder17 (actions.recipe_2x2 clicks, gui cursor, an env whose macros have NETH_VEC_NORECIPE; vecenv.c x2_label):
 * the policy's steps under the inventory: [55] its 2x2 crafts (the result shift clicked), [56] the result picked up by
 * a plain click; [57] its steps there, [58] of them with no op (sits), [59] with a click; the 2x2 labeller's runs [60]
 * started (a tidy window, a 2x2 plan), [61] followed to the result taken (every op the label's), [62] left (another
 * op); [63] the steps it says the close (no run: after the result, off a run, no 2x2 plan), [64] of them closed by
 * the policy; [65] its labels that decode back to another op (a bug); the runs left [66] at their first op (the part's
 * stack picked up), [67] at a middle op, [68] at the result's shift click; [69] by a close, [70] by a click on the
 * label's slot with another button or mode, [71] by a click on another slot; [72] the clicks on another stack of the
 * part (actions.x2_source's substitutes: left with first, followed with any). ([50] to [54] are lane/ladder18's) */
#define NETH_VEC_GUI_BOOKS 73
struct neth_vec;
int neth_vec_gui_books(const struct neth_vec *v, int64_t *out);
enum { NETH_VEC_EXPLORE_MAX = 1024 };

/* The buffers, each [n] by env id (a group's envs are ids g * n / G ..),
 * the trainer's to map once; page aligned. actions is the trainer's to
 * write before send; the rest the workers write during a step and are
 * valid from recv until the group's next send. */
struct neth_vec_bufs {
    int32_t *actions;           /* [n][16] ([n][17] with NETH_VEC_HOTBAR) */
    float *scalars;             /* [n][NETH_VEC_NSCALAR]: semteach.scalars for the next action */
    float *rewards;             /* [n]: the last action's reward */
    uint8_t *terminals;         /* [n]: never set (no state of this game ends an episode) */
    uint8_t *truncations;       /* [n]: the episode ended with the last action, by its clock or (task mode) by the
                                 * task's own end; terminations, last in the struct, says which */
    uint8_t *driver;            /* [n]: who drove the last action: 0 the policy (no macro, or the mine lock its
                                 * own attack started: Macros.chosen), else the macro's kind (the learner masks it) */
    uint8_t *kind;              /* [n]: the macro kind that drove it (0 none) */
    uint8_t *chosen;            /* [n]: Macros.chosen */
    struct neth_rl_act *acts;      /* [n]: the act the last action became (what the engine stepped) */
    int64_t *te;                /* [n]: the policy's steps into the episode */
    void *obs;                  /* the camera's own host buffer: neth_rl_semantic's [n][h][w][4] uint16 cells, or
                                 * neth_rl_raycast's [n][h][w][3] uint8 RGB (NULL: a pixels camera, on the device) */
    int64_t obs_shape[4];
    int32_t obs_bytes;          /* an element's bytes: 2 the semantic cells, 1 the ray cast's RGB */
    int32_t obs_device;         /* 1: obs is on the device (engine.raycast device), drawn by neth_vec_cast */
    int32_t *picks;             /* [n]: lane/picks, the trainer's pick for the next action (0 none, 1 + a craft plan) */
    uint8_t *mask;              /* [n][NETH_VEC_PICKS]: the picks the next action may take (0 always; with picks off,
                                 * or while a macro runs, 0 alone) */
    struct neth_vec_label *label;   /* [n]: lane/ntbc, the act the last action became in the policy's own action */
    /* lane/ticks-per-step: a step of T > 1 ticks (client.ticks_per_step x action_repeat; both NULL at T 1). Tick k
     * (1 .. T - 1) of env id's last step ran tick_acts[id * (T - 1) + k - 1] when tick_macro[id * (T - 1) + k - 1]
     * is 1 (a macro made it), else the hold-only act of the tick before's holds (acts[id]'s, or the last
     * macro-made tick's): what the recorder needs to write the step's every tick as the oracle's script */
    struct neth_rl_act *tick_acts;
    uint8_t *tick_macro;
    int32_t ticks;              /* T */
    /* lane/audit-fixes: [n]: 1 where truncations is 1 and the task itself ended the episode (task mode: its pickup
     * or a death, task_step's done); 0 where the clock did, and always 0 in milestone mode (a death there respawns).
     * The learner bootstraps a terminated row from zero, a timed-out one from its own value. It is last on purpose:
     * the buffers are shared through libneth.so, and a trainer built before it reads the members it knows at their
     * old offsets (in the middle, every field after it sat one slot on: the old binary read the observation's shape
     * shifted and stopped) */
    uint8_t *terminations;
};

/* lane/ntbc (distillation to fewer macros, train/distill.h): the act the engine stepped (bufs.acts) said in the
 * 16-wide action, so a student without the macro that made it can learn it. The camera's two bins are the nearest of
 * the 41 mu-law bins to the look's change (an absolute look less the yaw and pitch the act was made from, the yaw's
 * wrapped into -180..180), res what the bins miss in degrees; the keys from the holds (attack as mine, use as place)
 * and the drop's press. mbit names the macro that drove the step by its switch (NETH_VEC_MINE for the mine lock,
 * NETH_VEC_STRIP the strip and its lava escape, NETH_VEC_SEEK, NETH_VEC_FIGHT, NETH_VEC_FOOD the hunt and eating,
 * NETH_VEC_LOOT, NETH_VEC_SURFACE, NETH_VEC_CRAFT every craft plan; NETH_VEC_LBL_SCREEN the screen macro, always
 * on; 0 the policy alone). 32 bits wide (lane/mbit32): the chain's macros have switches from 1 << 17 up (obsidian,
 * nether, pearl, stronghold, dragon), and a 16-bit mbit cut their labels to 0; NETH_VEC_LBL_SCREEN stays 0x8000
 * (NETH_VEC_PICK's switch, which mbit_of never names: the pickaxe's plans label as NETH_VEC_CRAFT) */
enum {
    NETH_VEC_LBL_VALID = 1,     /* a decision step (not a reset's, whose act is none) */
    NETH_VEC_LBL_GUI = 2,       /* the act has GUI ops: the action cannot say them (lane/ladder10: with actions.gui
                                 * cursor a single click on a cell of the cursor grid, or a close, under a container
                                 * screen is said as the cursor's bins and the click's keys: mine a left click, place
                                 * a right one, sneak with mine a shift click, the hotbar element a number key's swap,
                                 * key 12 the close; and the inventory key's press as key 12; then not set) */
    NETH_VEC_LBL_HOTBAR = 4,    /* the act changes the hotbar slot: nor this (but NETH_VEC_HOTBAR's element, hotbar) */
    NETH_VEC_LBL_FAR = 8,       /* a look change past the outer bins (90 degrees) or a bin's half gap off */
    NETH_VEC_LBL_PRESS = 16,    /* a press the holds do not make (a key pressed and not held) */
    NETH_VEC_LBL_CHOSEN = 32,   /* Macros.chosen: the macro started from the policy's own action this step */
    NETH_VEC_LBL_SHADOW = 64,   /* lane/ladder6: the labeller's act on the step (neth_vec_level): computed beside the
                                 * policy's, never stepped (the env stepped the policy's action) */
    NETH_VEC_LBL_FITS = 128,    /* lane/ladder14: the state shows the inventory, a crafting table or a furnace, tidy, and
                                 * the plan craft_plan names fits it (NETH_VEC_NOTABLE's recipe start would begin there,
                                 * the table or furnace under the crosshair remembered as recipe_start does), whatever
                                 * the env's macros: distill.window_label's jump */
};
#define NETH_VEC_LBL_SCREEN 0x8000
struct neth_vec_label {
    int8_t cam[2];              /* the pitch bin, the yaw bin (NETH_VEC_CAM / 2: no look) */
    uint8_t flags;              /* NETH_VEC_LBL_* */
    uint8_t kind;               /* bufs.kind's */
    uint16_t keys;              /* bit j: key j of the action held (0..8 and 10) */
    uint8_t half;               /* lane/ladder12 (in what was padding): a craft step's half, 0 none (not a crafting
                                 * plan, or no craft), 1 the table (NETH_VEC_NOTABLE's), 2 the recipe (NETH_VEC_NORECIPE's),
                                 * 3 the recipe's first step (NETH_VEC_RECIPEKEY's jump; the recipe's otherwise), 4 the
                                 * table half's first step with no screen open (NETH_VEC_TABLEKEY's key; the table's
                                 * otherwise); 5 (actions.table_chance on, a step no macro drives) no screen open
                                 * and a crafting plan named: the table key's chance (distill.table_label); 6 (mbit
                                 * NETH_VEC_SEEK, lane/ladder16) the seek's first step (NETH_VEC_SEEKKEY's key); 7
                                 * (actions.seek_chance on, a step no macro drives at a SEEKKEY level) the seek's
                                 * start holding: the seek key's chance (distill.seek_label); lane/ladder18
                                 * (actions.seek_walk or seek_dig policy): 8 a seek step in the policy's half (its act
                                 * the student's label), 6 then the first step of a run in the macro's half (the key);
                                 * 9 (lane/ladder17,
                                 * actions.recipe_2x2 clicks; mbit NETH_VEC_CRAFT) a 2x2 recipe's step: the craft
                                 * macro's own (its clicks and its close), or with NETH_VEC_LBL_SHADOW the 2x2
                                 * labeller's on the policy's own inventory step (vecenv.c x2_label) */
    uint8_t pad_;
    uint32_t mbit;
    float res[2];               /* pitch, yaw: the change asked less the bins' (degrees) */
    int32_t hotbar;             /* lane/nthotbar: the hotbar change in NETH_VEC_HOTBAR's element: 0 none (or to the
                                 * slot already selected), k slot k - 1 (NETH_VEC_LBL_HOTBAR set) */
};

/* A finished episode (Loop.finish's fields) */
struct neth_vec_episode {
    int32_t env, logs, crafted, ach, deaths;
    uint32_t ms;                /* bit i: reward.MILESTONES[i] reached */
    double ret, net;            /* the return (float64 sum of the float32 rewards), math.hypot from the first step */
    float value;                /* the inventory's value at the end */
    int32_t start;              /* the start it began from (neth_vec_opts.starts: 0 the make's) */
    int64_t macro[NETH_VEC_MACROS];   /* the policy's steps by the kind that drove them */
    int64_t steps;              /* the env-steps the episode ran (its reset step included) */
    double t;                   /* the wall clock when recv recorded it (s, CLOCK_REALTIME) */
    int32_t items[NETH_VEC_ITEMS];    /* semteach.ITEMS' counts held at the end (lane/epitems) */
    int32_t extra[8];           /* lane/liquidlook: the counts held at the end of a water bucket, a lava bucket,
                                 * obsidian, an iron pickaxe, a diamond pickaxe, blaze rods, pearls, eyes */
    int32_t dims;               /* bit 0 the Nether, 1 the overworld, 2 the End: the dimensions the episode was in */
    int32_t obsidian;           /* stat.mineBlock of obsidian at the end */
    int32_t kills;              /* stat.mobKills at the end (lane/fight) */
    int32_t level;              /* lane/msstarts: the saved start's level (save_item -1), else 0 */
    int32_t seen[NETH_RL_SEEN];    /* lane/seen: the steps each of neth_rl_state's seen blocks was in view */
    int32_t cells;              /* lane/explore: the 4x4x4 cells under y 56 the episode entered (at most
                                 * NETH_VEC_EXPLORE_MAX), counted with the explore term off too */
    int32_t food;              /* lane/food: the food level at the end (a picked-up meat eaten shows here) */
    int32_t win;               /* lane/audit-fixes: 1 the episode ended by its task's pickup (task_step's done 1: the
                                * env's own task, task_of's v->tk or v->tk2), else 0 (a death, the clock, no task) */
};

struct neth_vec;

struct neth_vec *neth_vec_make(const struct neth_vec_opts *o, char *err, int en);
void neth_vec_free(struct neth_vec *v);
/* the binding under it (neth_rl_semantic, neth_rl_stats, ...): do not step it */
struct nwrl *neth_vec_env(struct neth_vec *v);
int neth_vec_buffers(struct neth_vec *v, struct neth_vec_bufs *b);
/* lane/nthotbar: bufs.actions' row, NETH_VEC_ACTION (+ 1 with NETH_VEC_HOTBAR) */
int neth_vec_action_width(const struct neth_vec *v);
/* the group's index, -1 with err (an env's engine stopped: its message) */
int neth_vec_recv(struct neth_vec *v, char *err, int en);
/* recv without the wait (lane/ntcast): the group, -2 when none is done now, -1 with err */
int neth_vec_try_recv(struct neth_vec *v, char *err, int en);
/* 0, -1: g out of range or not received */
int neth_vec_send(struct neth_vec *v, int g);
/* engine.raycast device (lane/rcdev): group g's frames drawn on stream (a cudaStream_t, the trainer's: its reads of
 * obs follow on it) after recv returned g; send waits for it and gives the envs its seen records (a send without a
 * cast casts first). 0 (nothing to do with the host's cast), -1 a failed launch */
int neth_vec_cast(struct neth_vec *v, int g, void *stream);
/* the k received groups gl[k] cast in one launch on stream (lane/ntcast), each group's send waiting for it as for
 * neth_vec_cast's; 0 or -1 */
int neth_vec_cast_groups(struct neth_vec *v, const int *gl, int k, void *stream);
/* group g's cast (received, cast on the device, not yet sent) to path: rcdev_dump, the kernel bench's input; 0 or -1 */
int neth_vec_raycast_dump(struct neth_vec *v, int g, const char *path);
/* up to max records, oldest first; how many (a full ring drops its oldest) */
int neth_vec_episodes(struct neth_vec *v, struct neth_vec_episode *out, int max);
/* out[0] the policy's env-steps, [1] the reset steps, [2] the workers'
 * ns in the vector env's own work (the act and the books), [3] the
 * trainer's ns waiting in recv, [4] ns in the resets, [5] episodes,
 * [6] records dropped, [7] the workers' ns stepping (neth_rl_stats' tick) */
void neth_vec_stats(struct neth_vec *v, double out[8]);
/* the pool's latency per env-step, summed since the pool started (divide by the steps): [0] the env's own step,
 * [1] the queue (done, not yet taken by a worker), [2] the frame's (a renderer's work), [3] the group's arrival
 * (ready, not yet polled: an env waiting for its group's slowest), [4] the trainer (polled, not yet stepped again:
 * its actor step and books); [5] the trainer's ns waiting in recv */
void neth_vec_lat(struct neth_vec *v, double out[6]);
/* lane/ownstarts: out[0] the saves started, [1] the ones that failed, [2]
 * the pool's starts, [3] the saves being made */
void neth_vec_saves(const struct neth_vec *v, double out[4]);
/* lane/starts: the mix's shares from now on (w[nstarts + 1], start 0 the make's; a sum over 0; a start at 0 is never
 * drawn): each later reset draws by them (the curriculum's stage weights spread over each stage's starts). The
 * trainer's thread, between recv and send like every call. 0, or -1 (no mix, a window, or a bad weight) */
int neth_vec_start_weights(struct neth_vec *v, const double *w);
/* lane/curric2: the start env id's episode plays from (its record's start: 0 the make's, 1 + k starts[k]); valid
 * while id's group is received; -1 out of range */
int neth_vec_env_start(const struct neth_vec *v, int id);
/* lane/seedwin: out[0] the window's loads, [1] its frees, [2] the mix's starts loaded now (every one without a
 * window), [3] the groups' lowest round */
void neth_vec_window(const struct neth_vec *v, double out[4]);
/* lane/tape: env id's next episode from the config's start (not a saved one) written to path as the Java oracle's
 * script: a Snapshot line, then each world tick's act as the env ran it (the policy's, a macro's, a reset step's no
 * act; moveact_script_line), closed at the episode's end. `make -C oracle script SEED=S CONF=WORLD.yaml FROM=START
 * SCRIPT=path TAPE=...` replays it on the config's world (CONF its world section; without it the oracle plays
 * config.yaml's, difficulty normal: lane/kaudit). Call it between recv and send of id's group. 0, or -1 (id or path bad) */
int neth_vec_tape(struct neth_vec *v, int id, const char *path);
/* lane/ladder6 (the removed macro as the labeller, mods/macros/shadow.c; off unless called): the student's level (macro
 * switches, cut to the make's) and the labeller's switch (NETH_VEC_SEEK, or NETH_VEC_SURFACE since lane/ladder9: the
 * swim labelled where the surface macro would start and run, mbit NETH_VEC_SURFACE). Call it once after the make
 * and any setter. Then neth_vec_env_level(v, id, 1) has env id play at that level from its next decision (call it
 * between recv and send, at an episode's start: its first episode, or the recv that ended the last), with the
 * labeller run beside it on each of its steps, its act never stepped: a step the labeller would drive (the level's
 * own macros left the step to the policy, and the seek would be running there, started and driven as it would be
 * at the level with the seek on) has its label replaced by the labeller's act (mbit NETH_VEC_SEEK, flags
 * NETH_VEC_LBL_SHADOW). 0, or -1 with err */
int neth_vec_level(struct neth_vec *v, int level, int labeller, char *err, int en);
int neth_vec_env_level(struct neth_vec *v, int id, int on);
/* lane/ladder11: the labeller NETH_VEC_CRAFT (mods/macros/shadow.c shc_step: the craft macro's state machine beside the
 * student's own L4b steps, its next act the label, mbit NETH_VEC_CRAFT; under a screen the student left untidy its
 * window tidied, with no plan that fits it closed). Its books over every env since the make, NETH_VEC_SHC_BOOKS of
 * them: labels from the macro's run, from the tidy, closes; of the run's labels, the student's act unequal and
 * changing nothing (the macro's state rolled back), unequal and changing something (the run started again), runs
 * started, runs ended, the student's act equal. 0, or -1 */
#define NETH_VEC_SHC_BOOKS 9
int neth_vec_shc_books(const struct neth_vec *v, int64_t *out);
/* and the run's labels by plan (neth_vec_pick_name(plan + 1); 22 rows, the last any other), six each: the label asks the
 * inventory key, use, a hotbar change, a GUI op, else the look or nothing; and the student's act equal to it */
#define NETH_VEC_SHC_PLANS (22 * 6)
int neth_vec_shc_plans(const struct neth_vec *v, int64_t *out);
/* (after neth_vec_level) the labeller's held attack kept in its label only where the student's look is within deg
 * degrees of the labeller's aim in yaw and pitch (the label otherwise the turn alone, no attack, no slot change); 0
 * (the default) keeps every act as the labeller made it. 0, or -1 (no level set, deg below 0) */
int neth_vec_level_align(struct neth_vec *v, double deg);
/* (after neth_vec_level) the labeller starts a run only on a seen cell within blocks of the eye (the seek's own
 * seek_want distance); 0 (the default) any distance, as the seek at the level would. 0, or -1 */
int neth_vec_level_reach(struct neth_vec *v, double blocks);
/* lane/ladder18 (after neth_vec_level; actions.seek_walk policy): beside the labeller, on a level episode's policy step
 * where the seek's start holds on a cell farther than the dig's reach (the walk the policy's), the seek runs as a
 * labeller (its own state, never stepped) and its walk act is the label (mbit NETH_VEC_SEEK, NETH_VEC_LBL_SHADOW, half
 * 8), until its cell is within reach (the dig half: the key's, labelled by seek_chance) or it ends. 0, or -1 */
int neth_vec_level_walk(struct neth_vec *v, int on);
/* lane/pearl: env id's last finished episode as NETH_VEC_PEARL booked it (mods/macros/pearl.c; zero before one ends, and
 * kept without the bit too: deaths and their causes) */
#define NETH_VEC_PEARLS 12
struct neth_vec_pearl {
    int32_t seen;               /* endermen seen (distinct ids in neth_rl_far) */
    int32_t provoked;           /* targets that screamed while stared at */
    int32_t killed;             /* targets whose kill showed (stat.mobKills up, the target gone from near the eye) */
    int32_t pearls;             /* pearls and eyes held at the end */
    int32_t pillars;            /* pillars built */
    int32_t step[NETH_VEC_PEARLS];  /* the episode's step pearls and eyes first reached k + 1 (-1 never) */
    int32_t deaths, death_step; /* the deaths, the episode's step of the first (-1 none) */
    int32_t chain;              /* pearls and eyes held when the chain ended: at the first death (keep_inventory off:
                                 * a respawn has nothing), else at the end */
    int32_t cause[4];           /* the first deaths' causes: an EntityList egg (survival.h EGG_*: what killed the
                                 * player), or 100 burning, 101 drowning (air 0), 102 starving (food 0), 103 other */
    int32_t phase_steps[12];    /* the steps in each phase (mods/macros/pearl.c PP_*) */
    int32_t phase, on, wtime;   /* now = 1: the phase, whether the macro runs, the day's time, */
    int32_t tid, angry, nfar;   /* the target, it screamed, the endermen in view, */
    float tdist;                /* the eye's distance to the target's middle */
    /* lane/pearlgo: the census (counting only). why: every step by what held the hunt (pearl.c PG_WHY_*: driving, dead,
     * the Nether, the End, a screen, no blaze held, 12 held, day, yielding, another macro, the policy); whom: the
     * steps the hunt was wanted and another drove, by dbench's driver (1 + the switch's bit, 0 the policy); hits: the
     * hunt's hits on its enderman; first: the episode's step each of the funnel's first reached (PG_F_*, -1 never);
     * wtime0: the day's time at the episode's first step */
    int32_t why[12];
    int32_t whom[33];
    int32_t hits;
    int32_t first[10];
    int32_t wtime0;
    int32_t hunt_y[8];          /* lane/pearlgo: the hunt's steps by the feet's y: under 40, 40-50, 50-56, 56-60, 60-64,
                                 * 64-70, 70-80, 80 and over */
    int64_t cen[8];             /* lane/pearlgo: the privileged census's sums (ntloop --pearl-census; never a macro's
                                 * input), over the overworld's living steps: steps, endermen alive within 64
                                 * (horizontally), of them under the sky, in the cone with the line clear; the same over
                                 * the hunt's steps (steps, alive, under the sky, in view) */
    int32_t hsky;               /* lane/pearlgo: the census's hunt steps with the player's feet at or over its column's
                                 * height map (under the open sky) */
    int32_t hbiome[64];         /* lane/pearlgo: the census's hunt steps by the player's biome (id & 63) */
    int32_t vsee[4];            /* lane/pearlsee: the hunt's steps with an enderman in the macro's own view (pearl_see's
                                 * rule), by the nearest's distance from the eye: under 16, 16-32, 32-48, 48-64 */
    int32_t towers, turns;      /* lane/pearlsee: pearl_tower's towers topped out; pearl_look turn's full turns begun */
    int32_t dips;               /* lane/pearlsee: pearl_dip turn's turns out of a dip */
    int64_t cen2[4];            /* lane/pearlsee: the privileged census over the hunt's steps, endermen within 64
                                 * (horizontally): tracked by the client, the line from the eye clear, from 4 over the
                                 * eye clear, under the sky with the line from the eye clear */
    int32_t upx[10];            /* lane/pearlsee (2nd pass): the pillar's climbs (PP_UP, not the hide's climb out) by how
                                 * each ended (pearl.c UX_*: its height reached, short for an enderman or zombie, out of
                                 * cobblestone in the hotbar, the phase's step cap, a roof (two short jumps), the macro
                                 * ended (another took the step), the night over, another phase, a hide's climb done) */
    int32_t uph[12];            /* the same climbs by the blocks placed when each ended (11: 11 or more) */
    int64_t cen3[8];            /* lane/pearlsee: cen2's census, the line clear from 8, 12 and 16 over the eye, from the eye
                                 * on the high ground within 48 (rlbind.h neth_rl_census hi_*) and 10 over it; and over the
                                 * hunt's steps with high ground found: steps, the sum of its height over the feet, of its
                                 * distance */
    int32_t sky_climbs;         /* lane/pearlsee: pearl_sky's look ups that met a ground roof (a dug tower begun) */
    int32_t dig_open, dig_steps;    /* its dug towers that reached the open sky; the steps mining over the head */
    int32_t drops, fetched, fetch_out;  /* lane/pearlsee: kills' drops booked, walked onto, given up (the cap) */
    int32_t high_steps, high_walks, high_gain;  /* lane/pearlsee: pearl_high's steps walking to high ground, its walks
                                                 * that ended at a tower try, the feet's height gained over them */
    int32_t lure_digs;        /* lane/pearlsee: pearl_lure's steps digging a tall tower down for a lost provoked one */
    int64_t cen4[16];           /* lane/pearlsee (3rd pass): the privileged census over the hunt's steps by phase (walk,
                                 * up, scan, the rest), four each: steps, steps with an enderman within 64 (3D) and the
                                 * line from the eye clear, such enderman-steps, of them inside the 70-degree frame */
    int32_t cany_f[4];          /* the steps by phase with one of those inside the frame */
    int32_t cyaw[4], cpitch[3]; /* the scan's clear lines by the yaw off the look (under 35, 35-70, 70-120, over 120) and
                                 * by the pitch off it (over the frame's top, inside, under its bottom) */
    int32_t restocks;           /* lane/pearlsee (3rd pass): pearl_restock's swaps of the pack's cobblestone in */
    int32_t oracle_steps;       /* lane/pearlsee (3rd pass): pearl_oracle's steps walking to the census's enderman */
    int32_t oracle_d[4];        /* those steps by its distance (horizontally): under 16, 16-32, 32-64, 64 and over */
    int32_t oracle_ups;         /* its pillars for one near, over the eye and hidden */
    int32_t oracle_path;        /* its steps on the pathfinder's way (walk_to), the rest straight */
    int32_t oracle_bad;         /* its targets marked with no path (pearl_reachable) */
    int32_t item_steps;         /* steps with an ender pearl item within 32 blocks (the census, PRIVILEGED) */
    int32_t item_walk;          /* pearl_oracle's steps walking to it */
    /* lane/pearlhome (pearl_home): the travel leg's steps (day and night), steps stood on land waiting for the night,
     * the first step on land (feet on the ground, no water within PH_SHORE; -1 never), the room's distance at the
     * episode's first leg step and at its end (-1 never asked), legs given no way */
    int32_t home_steps, home_stand, home_land, home_d0, home_d1, home_noway;
    int32_t home_digs;          /* lane/pearlhome (pearl_target any): steps digging the way to one under the feet */
    /* lane/pearlhome (lane/chain's frontier): the first death's place (the feet a step before), the macro kind running
     * (vecenv.c M_*), the pearl macro on and its phase (pearl.c PP_*: 11 pearl_home's leg), the leg walking, the
     * steps in a row the feet had been in water */
    float death_x, death_y, death_z;
    int32_t death_kind, death_on, death_phase, death_leg, death_wet;
    int32_t home_air;           /* lane/pearlhome: the leg's steps swimming up for air */
    /* lane/pearlhome (2nd pass, counting only): the steps with a pearl item in the census (PRIVILEGED): new items seen
     * (the census's count rising), by what held the step (why's classes), by the pearl phase when it drove, by the
     * nearest item's distance across (under 1.5, 1.5-6, 6-16, over) and its height against the feet (under -1.5,
     * within, over 1.5), by pearl_oracle's item walk (walk_to no path, walking, there, not walked: its time over), with
     * the pack unable to take a pearl, with the eye in water */
    int32_t it_new, it_why[12], it_ph[12], it_d[4], it_dy[3], it_wk[4], it_full, it_wet;
    int32_t death_mbit;         /* the first death's step's macro by its switch bit (the label's mbit; 0 the policy):
                                 * death_kind's M_* is shared by most macros */
    /* lane/chain (counting only): how a provoked target's lock ended: killed, the player dead, let go in daylight
     * (the clock 23000..12500: the sun's teleports), let go at night (lost from view, teleported, out-stared, too far),
     * another target taken while it was held; still held when read is provoked less these */
    int32_t prov_end[5];
    /* lane/pground (counting only; set with now = 1): the target's last place (the box's foot), whether the target is
     * in the macro's view list now, the macro's clock since its last swing at it (the scan's PEARL_HIT gate) */
    float tx, ty, tz;
    int32_t tvis, thit_age;
};
/* now 0: the last finished episode's; 1: the episode's so far (between recv and send of id's group) */
int neth_vec_pearl(const struct neth_vec *v, int id, int now, struct neth_vec_pearl *out);
/* lane/pnight (counting only): env id's pearl swing books for its episode so far (read between recv and send): [0]
 * pearl_oracle's in-reach steps given to the path (pearl_reach_aim: no enderman under the crosshair), [1] its in-reach
 * swings pressed with no enderman under the crosshair, [2] all its in-reach swings */
int neth_vec_pearl_aim(const struct neth_vec *v, int id, int32_t out[3]);
/* lane/pdig (P2, counting only): env id's pearl_patrol books for its episode so far: [0] its steps, [1] the cell ahead
 * walked on (open, an edge, shallow water), [2] a step up jumped, [3] a block of the way dug, [4] a drop of 1 to 3 walked
 * off, [5] turns from a refused cell or a stuck walk, [6] the sky dig's look up (steps), [7] steps under a roof */
int neth_vec_pearl_patrol(const struct neth_vec *v, int id, int32_t out[8]);
/* lane/pnight (counting only): env id's hunt steps with no macro bit on the step (the pearl book's PG_WHY_POLICY) for its
 * episode so far, by what took them: [0] provision, [1] the hunt not a candidate, [2] a candidate and no macro */
int neth_vec_pearl_gate(const struct neth_vec *v, int id, int32_t out[3]);
/* lane/pdig (counting only): which branch of env id's last step's dispatch made its act: VLS_MACRO the macro path (a
 * running or started macro, the policy, a respawn), VLS_AIR the air rescue, VLS_SHORE the shore exit, VLS_THREAT
 * threat_interrupt's step; under food_provision (8 * the stage (1 surface, 2 stock) + VLP_*) its exit, wood, its
 * completion, a screen closed, the swim up, an allowed macro run, the roam, standing (sneaking at night in stock) */
enum { VLS_MACRO, VLS_AIR, VLS_SHORE, VLS_THREAT };
enum { VLP_EXIT, VLP_WOOD, VLP_DONE, VLP_SCREEN, VLP_SWIM, VLP_RUN, VLP_ROAM, VLP_IDLE };
/* bits 8 up: the wood step's branch (VLW_*: a screen closed, a route chosen this step, on the way with the attack held
 * (a block in the way dug) or not, at the log with the attack held or not; + 8 on the craft route, else the bridge) */
enum { VLW_NONE, VLW_SCREEN, VLW_ROUTE, VLW_DIG, VLW_GO, VLW_CHOP, VLW_AIM };
int neth_vec_lsrc(const struct neth_vec *v, int id);
/* lane/lava (books only, read only; chain --lava-log; mods/macros/lavalog.c): what env id's macros could know of lava at
 * its last step's end. hot_*: lava (source or flowing) in view within LV_VIEW_R of the eye (obsview OV_HOT: its centre
 * seen past every block within 75 degrees of the look; n the cells in the cone before the line, d the nearest seen, -1
 * none); edge: lava_edge's test (surface.c surface_lava_edge: on the ground, lava at the floor's level or under open air
 * within 3 beside the feet); st_hot, st_near: strip2's lava ahead and lava near the feet in view at the strip's
 * direction sdir; ob_hot: the obsidian search's hot lava ahead this step; esc: the lava escape running; lava_t: the last
 * lava hit's env step (steps: the env's step now); lsrc: neth_vec_lsrc's low bits. -1 without macros or a bad id */
struct neth_vec_lava {
    int32_t hot_n, hot_x, hot_y, hot_z;
    float hot_d;
    int32_t edge, st_hot, st_near, ob_hot, esc, sdir, lsrc, on_ground;
    int64_t lava_t, steps;
    float water_d;              /* water in view (obsview OV_WATER: a source at the surface) within LV_VIEW_R, -1 none */
};
int neth_vec_lava(const struct neth_vec *v, int id, struct neth_vec_lava *out);
/* lane/pdig (counting only): env id's last step's last walk primitive call: out[0] the code (WK_*: 0 none; walk_to 16 +
 * 1 no far state, 2 no answer yet, 3 no path (its -1), 4 an older cell's no path (the new answer awaited), 5 the kept
 * part walked (the rest awaited), 6 there, 7 walking; sho_follow 32 + 1 there, 2 the retry wait after a plan found
 * nothing, 3 airborne (forward), 4 a plan found nothing, 5 a block dug too long (another way), 6 a frame, chest or
 * bedrock in the way (another way), 7 a block in front unsafe to dig, 8 digging, 9 no bridge face, 10 no cobblestone
 * (or fetching it), 11 bridging, 12 walking, 13 walking sneaking (SHO_SNEAK_F); the food census walk's straight walk 48 + 1 the pillar's dig up, 2 the
 * pillar placing, 3 walking, 4 walking a detour), out[1] the target's distance across, out[2] its height over the feet,
 * out[3] the way's length (walk_to: its path's length, sho_follow: its cells); the other walks 24 + 0 the pearl oracle
 * walk's straight walk (walk_to found no path), 1 the food scout's spiral leg, 2 its turn on the spot, 3 its detour;
 * out[4] on a walking step the cell ahead (macros.c wk_front: 1 open, 2 a step up, 3 a step up with no room, 4 a wall
 * two high, 5 an edge, 6 a drop of two or more, 7 a liquid, 8 not loaded, 9 a block at the head only, 10 the client
 * world's blocks there not the server's), 0 not read;
 * out[5] on sho_follow's walking steps the way's next cell: its height over the feet + 4 (0 to 8), + 16 x its cells
 * across from the feet's (0 to 3), + 64 the wood_drop case (sneak let go); on the spiral's leg: the eye in water (1),
 * on the ground (2) */
enum { WK_WT = 16, WK_OTH = 24, WK_SHO = 32, WK_FOS = 48 };
int neth_vec_walk(const struct neth_vec *v, int id, int32_t out[6]);

/* C's own tables (reward.values() from the recipes, mulaw_center), what a
 * NULL in the opts takes */
void neth_vec_tables(float values[4096], double camdeg[NETH_VEC_CAM]);
/* lane/nether: env id's NETH_VEC_NETHER books for its episode so far (read between recv and send): the episode's
 * step of the arrival in the Nether, a fortress block first seen, the spawner first seen, the first rod held, 7 rods
 * held, the overworld again (-1: not yet); rods held now and at most, stat.mobKills, deaths and their causes (what
 * hurt last as the player's state shows it, on each loss of health: lava (the feet's or the head's cell is lava), else
 * a hostile but a pigman within 16 blocks in view, else fire (burning), else starving (food 0), else other; a landing
 * from over 3.5 blocks a fall), the walker's phase, the portal cell remembered (has, x, y, z), the dimension now */
struct neth_vec_nether {
    int64_t arrive, found, spawner, rod1, rods7, home;
    int32_t rods, rods_max, kills, deaths, phase, dim;
    int32_t cause[8];           /* lava, fire, fall, mob, starve, other */
    int32_t portal[4];
    double pos[3];              /* the player's position now (F3's) */
    /* lane/nether2: the first death's step and position (-1 none), the fortress cell and the spawner remembered
     * (has, x, y, z), health now, cobblestone held, the walker's travel mode (0 walk, 1 bridge, 2 pillar, 3 dig) */
    int64_t death_t;
    double death_pos[3];
    int32_t fort[4], sp[4];
    int32_t loc[3];            /* privileged candidate start: has, block-centre x and z; no chunks generated */
    float loc_d0;              /* player's distance to it when the Nether macro first located it */
    float hp;
    int32_t cobble, mode;
    int64_t scan_ns, nsteps;    /* netherview's scans' time, ns; the Nether steps (alive) */
    int32_t deflects, deflect_hits, ghast_seen;   /* lane/nether4: fireballs aimed at, hits pressed, steps a ghast in view */
    int32_t covers, hoods;      /* lane/nether4: cover blocks placed (the hoods' too), hoods built to heal in */
    int32_t goal[5];            /* lane/nether5: the search's goal (has, x, y, z, on lava) */
    /* lane/rod: the link's funnel, each its first step (-1 not yet): on the fortress, a blaze in view, a blaze struck, a
     * blaze fight won; the hits and the kills; the Nether's alive steps by who had them (the macro, off, the lava
     * escape, a screen); the feet's last cell on the ground before the first death */
    int64_t fortin, bseen, bhit, bkill;
    int32_t bhits, bkills;
    int64_t st_drive, st_off, st_esc, st_screen;
    int32_t death_ground[3];
    int32_t bstat;              /* lane/rod: blazes killed this episode by the stats screen (the player's credited kills:
                                 * a blaze dead of fire, a fall or lava after a hit is not one, and drops no rod) */
    /* lane/rod (nether_oracle, privileged): the paths made and found, the oracle lock's steps */
    int32_t opaths, opaths_ok;
    int64_t or_lock_steps;
    int32_t home_rods;          /* lane/rod: the rods held, and the health, the step the overworld was reached */
    float home_hp;
    /* lane/rod: blocks from the portal cell (portal[1..3]) now: |dx| + |dy| + |dz| from the feet's cell, -1 with no
     * portal cell or out of the Nether; and the best (least) of it this episode in the Nether, -1 none */
    int32_t portal_d, portal_dmin;
    int32_t entry[5];          /* opt-in generated enclosed piece: has, standing x,y,z, F_* kind; append-only ABI */
    int32_t free_slots;        /* empty main-inventory slots at the last Nether observation; append-only ABI */
    /* lane/nleg (books only, for --nether-trace): the macro's last dig, the newest step its drive held attack on a
     * block: the step (-1 none), the cell, its block, and two six-bit masks over its faces (+x -x +y -y +z -z): lava in
     * the client world (every loaded block, seen or not), and lava that nt_block (lava_sense's view) shows the macro */
    int64_t dig_t;
    int32_t dig[3], dig_b, dig_lava, dig_seen;
    int32_t refills;            /* lane/nleg (nether_dig_refill): dug cells filled again for lava's glow beside them */
    /* lane/nleg (books only, PRIVILEGED counts for the trace, never read by a macro): steps at the camp, the server's
     * blazes within 16 of the eye, and within the kept spawner's own cap box (its cell expanded 8, 4, 8: at 6 there the
     * spawner spawns no more) */
    int32_t camp_age, blz16, blzsp;
    /* lane/nleg item 1c (books only, PRIVILEGED): the client world's blazes with a clear block ray from the eye (any
     * distance; -1 none to test); of the cap box's, those whose centre is over the spawner's top (y > sy + 1), their
     * mean height over the spawner's cell and mean horizontal distance from its centre, in tenths */
    int32_t blz_los, blz_up, blz_dy10, blz_hd10;
    /* lane/rods (books only, for --nether-trace): the macro's state at its last step: the fight lock's living id (0
     * none), the locked kind, what holds the step (bit 0 a kill's drop walked to, 1 resting, 2 healing back along the
     * way, 3 the hood (hood > 0), 4 the shy from a far blaze, 5 backing off lava, 6 a look round, 7 the oracle's blaze
     * lock this step, 8 the wither tower, 9 the lava escape (the strip's)), the hood's state, nt_drive's return, the act's
     * held keys and presses (1 << PK_*), the blazes in the player's view and the nearest's distance from the eye (-1
     * none); and the stuck count of a lock out of reach */
    int32_t fid, fkind, doing, hood, rc, hold, press, bseen_n;
    float bseen_d;
    int32_t fstuck;
    int64_t drive_t;            /* the step those of nt_drive were made (the macro drove it; 0 never) */
    float pick_at[3];           /* lane/rods: the kill's drop being walked to (the lock's last place of the living), and */
    int32_t pick_age;           /* the steps the walk has driven (NT_PICK_AGE ends it); 0, 0, 0 and -1 with none */
    int32_t hood_at[3];         /* lane/rods: the hood's feet cell (with hood > 0), and the cell of it being placed */
    int32_t hood_k;
    /* lane/rods (books only, PRIVILEGED): the fight lock's blaze (fid): its distance from the eye, height over the eye
     * and a clear ray (lk_los -1 no lock found) */
    float lk_d, lk_dy;
    int32_t lk_los;
    /* lane/rods2 (the macro's books): the cover's presses at the feet cell it stands in (NT_COVER_MAX the cap) and the
     * steps left of its off spell after a block that could not go in (0 none) */
    int32_t cov_here, cov_off;
    int64_t eye_strikes;        /* lane/rods2 (nether_eye_strike): steps the eye strike drove */
    int64_t mouth_steps, mouths;   /* lane/rods2 (nether_mouth_cover): steps it drove, blocks it placed */
};
int neth_vec_nether(const struct neth_vec *v, int id, struct neth_vec_nether *out);
struct netherview_item;
int neth_vec_nether_items(const struct neth_vec *v, int id, struct netherview_item *out, int max);
/* lane/rods2 (books only, PRIVILEGED; a trace's, never a macro's input): round env id's eye (ex, ey, ez) the client
 * blazes within NETH_RL_FB_R and those with a line to the eye (netherview_blaze_lines), and the server's small fireballs
 * within NETH_RL_FB_R (ids and places, at most NETH_RL_FB_MAX; their count returned, -1 none) */
#define NETH_RL_FB_R 32.0
#define NETH_RL_FB_MAX 24
struct netherview_fireball;
int neth_vec_nether_fire(const struct neth_vec *v, int id, double ex, double ey, double ez, float yaw, float pitch,
                         int *blz, int *blz_los, float *los_d, float *los_dy, int *los_cone, int *los_mouth,
                         int los_cell[3], struct netherview_fireball *out);
/* lane/rods2 (books only): a clear block ray in env id's client world; 1 clear, 0 a block, -1 none */
int neth_vec_nether_clear(const struct neth_vec *v, int id, double x0, double y0, double z0, double x1, double y1,
                          double z1);
/* lane/rod: armor_kit's books (mods/macros/armor.c) for env id: the pieces worn (bit 0 boots, 1 leggings, 2 chestplate,
 * 3 helmet; read off the inventory screen's armor slots when its equip ran) and their points, the ores dug and given
 * up, the dig walker's plans and cells walked, the steps of the first iron ore held, the ingots wanted held, three and
 * four pieces worn (-1 not yet), the position now and dead now */
struct neth_vec_kit {
    int32_t worn, points, ores_dug, gave_up, plans, cells;
    int64_t t_ore1, t_ingots, t_worn3, t_worn4;
    double pos[3];
    int32_t dead;
    int32_t n_ore, n_ingot, n_pieces;       /* iron ore, ingots and armor pieces held now */
    int32_t kind, cplan, on;                /* the run's kind (vecenv.c M_*), its craft plan (C_*, -1), armor's on */
    /* snow_kit's (mods/macros/snow.c): snowballs held now, snow cells dug and given up, the steps of a shovel held and
     * of 16 and 64 snowballs held (-1 not yet) */
    int32_t n_snow, sn_dug, sn_gave_up;
    int64_t t_shovel, t_snow16, t_snow64;
    /* lane/snowleg (snow_first, snow_kit far): the wait given up (1), the snowy column (1 found, -1 none, 0 not looked
     * for) and its distance from where it was looked for, the trip's steps and legs with no way, logs dug, the snowballs
     * held on reaching the End (-1 not yet); the steps the wait began, the trip began, the trip found snow in the scan,
     * the End reached (-1 not yet) */
    int32_t sn_quit, sn_col, sn_col_d, sn_trip_steps, sn_noway, sn_logs, sn_end_snow;
    int64_t sn_w0, t_trip0, t_trip1, t_end;
    /* the column moved on (no snow there); the first death's step (-1 none), the snow run's part that drove the step
     * before (0 none: another macro or the policy, 1 the wood, 2 the trip, 3 the dig), steps since it last drove, the
     * cell, the air */
    int32_t sn_hops, sn_dpart, sn_dago, sn_dpos[3], sn_dair;
    int64_t sn_dstep;
    /* lane/supply: the ores picked as the walk's target, given up with no cell walked (no way) and after walking */
    int32_t picks, gu_noway, gu_walked;
    /* lane/supply (books): a placed furnace in reach now (common.c near_furnace, the macros' own memory) */
    int32_t furnace_near;
    /* lane/choice (books only, never saved): the armour craft slot (armor.c arm_choose, armor_choice): calls with a
     * piece offered, conflicts (a replacement iron pickaxe offered beside it), commits of the piece and of the pickaxe,
     * selections not offered, armor_choice=check's double queries and those that moved a byte or an offer; the last
     * conflict's env step (-1 none), armour plan (C_*), ingots, sticks, iron pickaxe damage, option committed (1 the
     * piece, 2 the pickaxe) and why (0 the default, 1 selected, 2 an invalid selection) */
    int64_t ch_queries, ch_conflicts, ch_armor, ch_ipick, ch_invalid, ch_check, ch_check_diff, ch_last_step;
    int32_t ch_last_plan, ch_last_ingots, ch_last_sticks, ch_last_dmg, ch_last_chose, ch_last_why;
};
int neth_vec_kit(const struct neth_vec *v, int id, struct neth_vec_kit *out);
/* lane/rod: a case's portal cell for env id, taken where its episode recorded none (a start away from its arrival) */
int neth_vec_nether_portal(struct neth_vec *v, int id, int x, int y, int z);
/* lane/obsidian2: env id's NETH_VEC_OBSIDIAN books for its episode so far (read between recv and send; kept without
 * the bit too): the step the Nether was first entered and the kit first held (-1 not yet), obsidian held now and at
 * most and the stat mined, the jobs started and the steps driven while the kit was held by who drove them (who, by
 * index: the policy, mine, fill, cast, douse, the climb above the lava layer, the walk to a remembered source, the look
 * around, the search's strip, the strip on its own, the lava escape, the seek, the surface, craft or food, another
 * macro: mods/macros/obsidian.h OW_*), deaths by what hurt last (lava: the feet or head in lava, or
 * burning within 300 steps of it; fire; a fall from over 3.5 blocks; a hostile in view within 16 blocks; drowning; hunger
 * at 0; other), and the first death's step, place, cause, who (for lava or fire: who drove the step its lava incident
 * began; else the last step's) and whether the kit was held. out/runtime/obsidian_case prints them */
struct neth_vec_obsidian {
    int64_t nether, kit, d_step;
    int32_t obsidian, obs_max, mined, deaths, d_cause, d_who, d_kit, dim;
    int32_t jobs[16], steps[16], cause[7];
    float d_pos[3];
    double pos[3];
    int32_t who_last, who_hurt;     /* lane/obsidian4: who drove the last live step, who began the last lava incident
                                       (a death's who, every death: obsidian_case's table) */
    int32_t sky_trips, sky_water;   /* lane/obsidian7 (obsidian_water sky): water trips begun, ended with water in view */
    int32_t drops;                  /* lane/obsidian7 (obsidian_drop on): the search's walks off onto a deeper floor */
    int32_t steers;                 /* lane/obsidian7 (obsidian_cast_way strip): the strip's turns toward far lava */
    int32_t rims;                   /* lane/obsidian7 (obsidian_cast_rim on): the casts poured on a rim */
    int32_t wb_fills;               /* lane/obsid: water buckets filled (any way), the first step one was held (-1 */
    int64_t wb_first, wb_steps;     /* never) and the steps one was held */
    int32_t scouts, scout_found;    /* lane/obsid (obsidian_water scout): scouts begun, water seen by their far looks */
    int64_t fn[8];                  /* lane/obsid: the link's funnel after the kit (mods/macros/obsidian.h OF_*): the
                                       first step of the head under the sky, water in view with an empty bucket, a water
                                       bucket held, lava in view with one, a cast begun, 1 and 10 obsidian, the Nether */
    /* lane/obsid2: the water trip: surface water kept now, trips begun, trips that reached their water; at the first sky
     * after the kit what the trip made of it (mods/macros/obsidian.h OSW_*: due, water held, no bucket, none kept, over
     * 128 across, over 8 up or down, night; -1 not reached) and the kept water's distance across then (-1 none); the
     * search's cells walked (obsidian_glance's count) and the walk's side steps (obsidian_walk around) */
    int32_t sw_ok, sw_trips, sw_fill, sky_why;
    float sky_d;
    int32_t glances, arounds;
    int32_t trip_end[5];            /* lane/obsid2: water trips ended short: under y 50, 10 under the start, night, out of
                                       air swimming, too long */
};
int neth_vec_obsidian(const struct neth_vec *v, int id, struct neth_vec_obsidian *out);
/* lane/strip2: env id's strip books for its episode so far (read between recv and send; kept without the switch
 * too): got the units each good rose by in the inventory (coal, iron ore, gold ore, diamonds, redstone, cobblestone:
 * what was picked up), the cells the strip walked into, the step of the last (-1 none) and where, the steps the strip
 * drove, the episode's steps, the player's place and dimension now, the diamond pickaxes held (the strip's last want
 * met). out/runtime/strip_case prints them. lane/strip3: held, what the last step held (diamonds, planks, sticks,
 * logs, tables, ingots, the best pickaxe's tier), table the table placed (0 none, 1 placed, 2 in reach), errand the
 * wood errand's books (mods/macros/strip2.c: [0] begun, [1] reached the surface, [2] logs got on it, [3] the diamond pickaxe
 * made after one), out the steps out of the depth's climb drove */
enum { NETH_VEC_DIA_N = 19 };      /* lane/diamond: struct neth_vec_strip dia's length */
struct neth_vec_strip {
    int32_t got[6], cells, dim, dpick;
    int64_t last, strip_steps, steps;
    float lpos[3];
    double pos[3];
    int32_t held[7], table, errand[4];
    int64_t out_steps, dpick_step;
    /* lane/diamond: the link from flint and steel to the diamond pickaxe (counted from the first step flint and steel is
     * held until a diamond pickaxe is): steps with the feet under y 16, at 16 to 39, at 40 and over; steps holding no
     * pickaxe; the step the last pickaxe was gone (-1 never), the feet's y then, the wood held then (planks, a log 4,
     * two sticks 1); the least wood held; diamond ore cells the crosshair was on (mo_block 56, a cell once); pickaxes
     * broken (a pickaxe's count fell with no screen open); the feet's y now; steps the iron pickaxe was craftable from
     * ingots but for the wood (3 ingots or more, no iron or diamond pickaxe, under 2 sticks and 2 planks); the seek's
     * steps by what it went to (iron ore, diamond ore, gravel, a log); climb_way pillar's levels and give-ups and the swims
     * climb_swim began (the whole episode) */
    int64_t dia[NETH_VEC_DIA_N];
};
int neth_vec_strip(const struct neth_vec *v, int id, struct neth_vec_strip *out);
/* lane/obsidian3: env id's NETH_VEC_BPORTAL books for its episode so far: starts, sites chosen and given up (fail_kind
 * by reason: mods/macros/bportal.c BF_*: no site, the dig, the slot's check, an aim, a pour, a trip, the walk to the work
 * position, the lighting), frame cells cast, the portal lit (and its first step, -1 never), the fill trips, uses sent,
 * the steps it drove, the most cells cast at one site, the build's plan step now (-1 no site), its mode and the sources
 * remembered */
struct neth_vec_bportal {
    int32_t starts, sites, aborts, casts, lit, trips_lava, trips_water, uses, fail_kind[8];
    int64_t lit_step, steps_on;
    int32_t best_casts, step, mode, nmem_water, nmem_lava;
    /* lane/obsidian4: lava pools filled at, lava buckets filled, lava the search found, the search's steps, walks back
     * to a held site, sites given up for want of lava */
    int32_t pools, fills_lava, search_found, back_walks, site_lost;
    int64_t search_steps;
    int32_t wet_who;            /* who drove the step the eyes last went under water (OW_*: a drowning's who) */
    int32_t site_why[8];        /* site tries by why refused: ok, given up, ground, feet, plane, lava near, water */
    int32_t climbs;             /* climbs begun to the search's height */
    /* lane/obsidian5 (policy.actions.bportal_roam surface): the walk over the surface's steps, the climbs to the sky's
     * steps, legs walked, far looks, climbs to the sky begun and ended under it, lava and water the far look brought
     * into view, the first lava's distance from the eye (-1 none) and step (-1), the farthest from where it began */
    int64_t roam_steps, up_steps;
    int32_t roam_legs, roam_looks, ups, ups_done, roam_lava, roam_water;
    float roam_lava_d;
    int64_t roam_lava_step;
    float roam_far;
    int32_t pillars, pillar_tops, pillar_lava;  /* bportal_roam pillar: begun, risen to the top, looks that saw lava */
    /* lane/obsidian6 (policy.actions.bportal_buckets 1 or any): sites begun and frame cells cast the one-bucket way,
     * lava buckets poured back into a pool (water wanted, the one bucket full of lava), waters poured beside a cell
     * before its lava trip */
    int32_t one_sites, one_casts, dumps, parks;
};
int neth_vec_bportal(const struct neth_vec *v, int id, struct neth_vec_bportal *out);
/* lane/food2: env id's food census for its episode so far (counting only, mods/macros/food.c; the dbench books only):
 * the first step the food level was at 6 or under (no sprint) and at 0 (starving), -1 never; at each of those two the
 * meat held, the other food held (FB_* bits: food.c FOOD_OTHER), the eye's y, who drove (the dbench driver index), an
 * animal in view; eatings begun and done, hunts begun and the meat they got, meat and other food picked up (each rise
 * of the count held), other food eaten; the steps at food 10 or under (hungry) and at 0, each by why it was not eating
 * (FW_*: eating, meat held with another macro driving, meat held with the policy driving, no meat with an animal in view,
 * no meat with other food held, nothing edible), and the hungry steps with meat held by the driver */
enum { FW_EATING, FW_MEAT_MACRO, FW_MEAT_POLICY, FW_ANIMAL, FW_OTHER, FW_NOTHING, FW_N };
struct neth_vec_food {
    int32_t low_step, zero_step, low_meat, zero_meat, low_other, zero_other, low_y, zero_y, low_mb, zero_mb;
    int32_t low_animal, zero_animal;
    int32_t eat_starts, eats, hunts, hunt_meat, meat_got, other_got, other_eaten, animal_steps;
    int32_t hungry[FW_N], starving[FW_N], meat_mb[32];
    /* the steps alive by food band (0: over 6, 1: 1 to 6, no sprint, 2: 0) in the overworld, the horizontal distance
     * walked in them (centimetres) and the steps of them under y 56 (the mine) */
    int32_t band_steps[3], band_cm[3], band_deep[3];
    int32_t scouts, scout_found;    /* food_scout: scouts begun, and ended with an animal in view (its hunt begun) */
    int32_t scout_end[6], scout_far;    /* ... ended by why (found, out of time, below the surface, water, night or
                                         * the hunt held off or a screen, cut short by another macro) and the farthest
                                         * each got from its start summed (blocks) */
    int32_t hunt_end[4];            /* hunts ended: meat got, the animal lost, out of time, other (lane/food2) */
    int32_t homes, home_end[3];     /* walks home (food_scout early) begun, and ended: arrived, given up, cut short */
    int32_t lost_why[5];            /* lost hunts: killed with the drop not picked up, hit not killed, never hit with
                                     * its last place reached, not reached, in water */
    int32_t trips, trip_steps, trip_meat;   /* scout trips (a scout to the end of its hunts and walk home), their
                                             * steps and the meat they got */
    int32_t low_held, deep_after;   /* steps at food 6 or under with food held (not eaten); steps under y 56 after a
                                     * trip ended (back at mining) */
};
int neth_vec_food(const struct neth_vec *v, int id, struct neth_vec_food *out);
/* lane/nether2: env id's client world block at a cell, -1 not loaded (nether_case --dump: a developer's map of a start;
 * no macro reads it) */
int neth_vec_nether_block(const struct neth_vec *v, int id, int x, int y, int z);
/* lane/picks: pick k's name (0 "none", then the craft plans': "planks", "table", ...), NULL past NETH_VEC_PICKS */
const char *neth_vec_pick_name(int k);
/* lane/macromod: the run file's macro names (actions.macros: train/loop.c macro_set, brutetab), from the macros'
 * registration table (csrc/mods/macros/macros.c, each macro's .sw): the i-th name and its bit (*bit when not NULL), NULL
 * past the last; a build without the macros (MACROS=0) has none */
const char *neth_vec_macro_switch(int i, int32_t *bit);
/* lane/dragon: env id's End books as its last step left them (between recv and send of its group): out[0] the
 * dimension, [1..3] x y z, [4] health, [5] the crystal copies its client holds, [6] crystals in view, [7] the dragon's
 * health (-1 no copy), [8] its death seen, [9] the macro's phase (vecdragon.h DP_*, 0 off), [10] arrows shot,
 * [11] crystals hit, [12] the step of the kill (-1), [13] of the exit portal (-1), [14] deaths, [15] endermen in view,
 * [16..19] the End's deaths by what was near (vecdragon.h died: the void, the dragon, an enderman, else), [20] sword
 * hits swung at the dragon, [21] the dragon's lowest health seen (-1 none), [22] arrows at the dragon, [23] the ones
 * its health fell after, [24] the step of the first death in the End (-1 none); lane/dragon2's books: [25] that
 * death's cause (vecdragon.h DK_*, -1 none), [26] pushed just before it, [27] the macro's phase then, [28..30] where,
 * [31] the fall's length, [32] [33] the hit before it (chatmsg.h DT_*, its name), [34] the dragon's head's distance,
 * [35] [36] the killing hit's DT_* and name, [37] wing pushes seen, [38..46] health lost in the End by DK_*, [47] the
 * food level at the death, [48] arrows held (the macro's last step), [49] the dragon's bites, [50] the food level,
 * [51] the dragon's health its arrows took, [52] its swings took, [53] swings that took any, [54] crystals the macro
 * keeps (seen, not seen to blow up), [55..63] the first three's id, hunts and arrows, [64] [65] the dragon's last
 * place seen (x, z: the exit portal's after its death), [66] the phase's sub-step, [67] tower blocks held, [68] the
 * hotbar's slot of one (-1), [69] the crosshair's block (-1 none), [70] its height over the feet, [71] the rise's
 * step; lane/dragon3: [72] lobs let go, [73] lobs whose crystal went, [74] arrows picked up, [75] the exit's walks
 * out from under the portal's floor, [76] the
 * boss bar's rises seen, summed (its healing), [77..79] spare; 0, or -1 for a bad id */
#define NETH_VEC_DRAGON_INFO 80
int neth_vec_dragon_info(const struct neth_vec *v, int id, double out[NETH_VEC_DRAGON_INFO]);
/* lane/legs: env id's crystal hunt as its last step left it (chain --hunt-trace; no macro reads it): out[0] the macro's
 * phase (0 off), [1] its sub-step, [2] the hunt's crystal id (0 none), [3..7] that crystal's kept record (x y z where
 * seen, hunts, shots; -1 when not kept), [8] the shot's target id, [9..11] its aim point, [12] shots from this stand,
 * [13] a lob, [14] the hunt stand's tower blocks, [15] crystals kept, [16] the hunted crystal in view now, [17]
 * crystals in view, [18] shots thrown in all, [19] crystals hit; 0, or -1 for a bad id */
#define NETH_VEC_DRAGON_HUNT 20
int neth_vec_dragon_hunt(const struct neth_vec *v, int id, double out[NETH_VEC_DRAGON_HUNT]);
/* lane/dragon2: env id's End view record (endview.h) as its last step left it, NULL for a bad id (the case's books) */
struct neth_rl_end;
const struct neth_rl_end *neth_vec_end_view(const struct neth_vec *v, int id);

/* lane/stronghold: NETH_VEC_STRONGHOLD's books of an episode: the step each was first reached (-1 never), the counts
 * at its end */
struct neth_vec_sh_book {
    int32_t started;            /* the step the macro first started (-1: never) */
    int32_t brick;              /* stone brick (98, 97, 101) first seen: the stronghold */
    int32_t frame;              /* an end portal frame first seen: the portal room */
    int32_t room;               /* standing in the portal room (fell through its roof) */
    int32_t lit;                /* every frame seen with its eye: the portal lit */
    int32_t end;                /* in the End */
    int32_t throws, near, lost; /* eyes thrown, throws that flew down, thrown eyes not picked up again */
    int32_t eyes;               /* eyes held at the end */
    int32_t filled;             /* eyes put in frames */
    int32_t deaths, death_step; /* deaths while the macro ran (latched, not over, or within 200 steps of giving up:
                                 * it stops where it is), the first one's step */
    int32_t cause[6];           /* those deaths by the cause read off the steps before: lava or fire, fall, drowning,
                                 * starving, a hostile in view, other */
    int32_t phase;              /* the phase at the end (mods/macros/stronghold.c SH_*) */
    int32_t gave_up;            /* the step the macro gave up (-1 never) */
    float dist;                 /* the horizontal distance from the start at the end */
    int32_t deaths_after;       /* deaths once it was over (the End and back, or later than that after giving up) */
    int32_t gave_y;             /* the feet's height when it gave up (the dig's floor, the room's) */
    int32_t snaps;              /* lane/stronghold2: far lines read as one chunk centre (the start's) */
    float snap_d, snap_d2;      /* the last far line's nearest chunk centre in the ring's band and the next: blocks
                                 * across the line */
    int32_t dropseen;           /* throws whose drop was seen lying (the rest of the lost: shattered or never seen) */
    int32_t picked;             /* eyes picked up again (thrown ones, and in the room) */
    int32_t room_eyes;          /* eyes held on reaching the room */
    int32_t stepped;            /* stepped off the frame row (away from its lava) when it stopped in the room */
    int32_t canopies;           /* drops on a canopy over the head cleared (leaves broken from below, up a pillar) */
    int32_t relocs;             /* throws' sites moved away from leaves round them */
    int32_t hides, hid;         /* lane/stronghold3: nights hidden in a covered hole, the steps spent in them */
    int32_t skies;              /* lane/stronghold3: climbs from under a roof to the sky before a throw or the walk */
    int32_t crafts;             /* lane/stronghold3: craft starts it gave the step to for a stone pickaxe */
    int32_t pillars;            /* lane/stronghold3: pillars up a stone wall the walk could not dig */
    int32_t stuck;              /* lane/stronghold3: walks no nearer the goal for SH_STUCK steps (stronghold_stuck) */
    int32_t hunts;              /* lane/stronghold3: hunts it started (stronghold_hunt) */
    int32_t flesh;              /* lane/stronghold3: rotten flesh eatings begun (stronghold_flesh) */
    int32_t opaths;             /* lane/shold: ways made by stronghold_oracle locate (PRIVILEGED) */
    int32_t osub;               /* ... its step at the end (stronghold.c SO_*) */
    int32_t bridges;            /* ... blocks it placed to bridge a gap (the portal room's, stairs to frames) */
};
/* lane/dbench (neth_vec_opts.dbench 1): env id's benchmark books for its episode so far (read between recv and send; a
 * group never sent again, as --score's last, keeps its episode's whole). ms_first[b] the episode's step (ep_steps, the
 * reset step 0) a milestone was first held, -1 never: b < NETH_VEC_MILESTONES reward.MILESTONES' bit, b ==
 * NETH_VEC_MILESTONES the Nether entered, NETH_VEC_MS_END the End entered, NETH_VEC_MS_DRAGON the dragon's death seen.
 * mb_stage[j][m] the policy-decision steps spent while the run's stage was j (train/stages.h STAGE_CHAIN: the last
 * chain entry reached, the Nether and the End counted once been there) by who drove them: m 0 the policy's own keys,
 * else 1 + the index of the driving macro's switch bit (label.mbit). Deaths (the first NETH_VEC_DB_DEATHS): the step,
 * the dimension (-1 Nether, 0, 1 End), the driver as mb_stage's m, whether the step or the one before was a landing
 * from over 3.5 blocks (fall), and whether the player burned (the step before's fire) */
#define NETH_VEC_DB_STAGES 16
#define NETH_VEC_DB_DRIVERS 33
#define NETH_VEC_DB_DEATHS 8
struct neth_vec_db {
    int32_t ms_first[NETH_VEC_MS_BITS];
    int32_t mb_stage[NETH_VEC_DB_STAGES][NETH_VEC_DB_DRIVERS];
    int32_t steps, ndeath;
    int32_t food0;              /* the step the food level was first 0 (-1 never) */
    int32_t d_step[NETH_VEC_DB_DEATHS], d_dim[NETH_VEC_DB_DEATHS], d_mb[NETH_VEC_DB_DEATHS];
    int32_t d_fall[NETH_VEC_DB_DEATHS], d_fire[NETH_VEC_DB_DEATHS], d_stage[NETH_VEC_DB_DEATHS];
    int32_t d_egg[NETH_VEC_DB_DEATHS];      /* the EntityList egg (survival.h EGG_*) the killed-by stat moved for within
                                             * 5 steps before the death (what killed the player), -1 none */
    int32_t d_air[NETH_VEC_DB_DEATHS], d_food[NETH_VEC_DB_DEATHS];   /* the step before's air 0 or less, food 0 */
    /* lane/speedrun-recipe: the stalls (neth_vec_opts.stall_ticks, stall_radius) at least stall_ticks long: their
     * steps, how many, the longest (the one open at the read counted as it stands) */
    int32_t stall_steps, stall_runs, stall_max;
};
int neth_vec_db(const struct neth_vec *v, int id, struct neth_vec_db *out);
/* env id's books: last 1 the episode before the current one (read after recv reset its group), 0 the current. 0, -1 */
int neth_vec_stronghold_book(const struct neth_vec *v, int id, int last, struct neth_vec_sh_book *out);
/* lane/proom: env id's stronghold latch and its holds as the last step left them (read between recv and send; read
 * only: the holds are recomputed here without the clocks sn_holds and sh_food_holds keep, so a trace moves no row).
 * go, phase (stronghold.c SH_*), osub (SO_*), on (it drives), fwait (the food hold's last answer); the snow_first hold
 * (sn_hold: on, under the snowballs it wants and not given up), its wait's first step and given-up flag; the
 * stronghold_food, stronghold_armor and sword_stock holds; want (sh_want's answer once latched); the oracle room
 * located (ok) and the horizontal distance to its middle (-1 none); the books' room, lit, filled and gave_up. */
struct neth_vec_sh_trace {
    int32_t go, phase, osub, on, fwait, sn_hold, sn_quit, food_pts, food_wait, armor, armor_wait, swords, sword_need,
        sword_wait, want, eyes, snowballs, room_ok, room, lit, filled, gave_up, book_phase, screen;
    int64_t fw1, sn_w0, wtime;
    double room_d;
    /* the oracle walk (stronghold.c sho_follow): the stand cell (ok, x, y, z), the way's cells and the one walked to,
     * plans failed in a row, steps off the way, the cell being dug, the spawner (has, x, y, z), the room's box */
    int32_t stand[4], nop, opk, ofail, ooff, dig[3], sp[4], box[6];
    int32_t prov, fs_home;      /* provision_mode's answer (PV_*: not 0 takes the step before any macro's test), the
                                 * provisioning and shore bits */
    int32_t sky, pearl_on, pearl_phase, pearls, hostiles;   /* the sky over the eye (shoracle_sky_here: the server's
                                 * blocks, a label for the trace only), the pearl macro running and its PP_* phase,
                                 * pearls and eyes held, livings in view (neth_rl_state nvis) */
    int32_t nframe, rstart;     /* end portal frames in view now (shview_scan, the macro's own scan, radius 16) and
                                 * stronghold_room_start's latch step + 1 (0 none) */
    float frame_d;              /* ... the nearest one's distance from the eye (-1 none) */
    /* lane/proom (order B): the provision wood route (provision.c provision_wood_step): its target log cell, the block
     * there now (the server's, PRIVILEGED, a label), its stand cell, logs held, and the steps since the route was set */
    int32_t wood[3], wood_blk, wood_stand[3], wood_logs;
    int64_t wood_age;
    /* lane/proom (order 9:43 PM (2a), where the swings go): the living under the crosshair (neth_rl_state mo_entity, its
     * kind) and the block (lq_hit, its id), the pearl macro's target (its id, screamed, distance) and its swings so far
     * (the books' hits) */
    int32_t mo_entity, mo_ekind, lq_hit, lq_block, ptid, pangry, phits;
    float ptdist;
};
int neth_vec_sh_trace(const struct neth_vec *v, int id, struct neth_vec_sh_trace *out);
/* lane/bportal: env id's portal build as the last step left it (read between recv and send; read only, chains only:
 * mods/macros/bptrace.c recomputes each start test without its side effects, so a trace moves no row). The items the
 * build wants (obsidian and the count the obsidian macro and the frame plan want, keep; buckets empty, of water, of
 * lava; flint and steel, diamond pickaxes, cobblestone, dirt, furnaces), the macro kind; the bucket portal's want
 * (BPW_*) and, the want held, its start test (BPS_*; BPS_GO with the mode it would take, BM_*), its state (on, mode,
 * site, plan step, the sources kept and whether one is near enough to go to, its rest's end); the obsidian macro's
 * want (OBW_*), its state (on, job, ob_who's owner, the search's strip, its rest, the sources kept); the frame plan's
 * want (PPW_*, the overworld's clauses), whether the craft macro runs it (pp_on), its step (-1 not running) and retry
 * step */
enum { BPW_OK, BPW_SWITCH, BPW_DIM, BPW_VISITED, BPW_DEAD, BPW_FURNACE, BPW_DONE, BPW_FLINT, BPW_BUCKETS, BPW_DIA,
       BPW_ONE_DPICK, BPW_ALWAYS };
enum { BPS_GO, BPS_NOWANT, BPS_SCREEN, BPS_WATER, BPS_REST, BPS_AIR, BPS_EAT, BPS_PROV, BPS_LAVA, BPS_SITE_LAVA,
       BPS_NO_WATER, BPS_NO_LAVA, BPS_FULL_BAD, BPS_NO_POOL, BPS_NONE };
enum { OBW_OK, OBW_SWITCH, OBW_DIM, OBW_VISITED, OBW_DEAD, OBW_HELD, OBW_DONE, OBW_TRIP, OBW_FLINT, OBW_BPORTAL,
       OBW_BUCKET, OBW_EARLY, OBW_DPICK };
enum { PPW_OK, PPW_SWITCH, PPW_DIM, PPW_FURNACE, PPW_VISITED, PPW_OBSIDIAN, PPW_FLINT, PPW_COBBLE, PPW_PROV, PPW_RETRY };
/* ob_start's last answer (obsidian.c, books only): begun, not wanted, a screen, the eye in water, its rest (ob.off), its
 * calm (ob.calm), obsidian_food's hunt, no job (nothing in view, remembered in reach, no climb, look, scout or trip), the
 * cast's lava too far across (obsidian_cast_way), the pool too small (obsidian_pool_min) */
enum { OBS_GO, OBS_NOWANT, OBS_SCREEN, OBS_WATER, OBS_REST, OBS_CALM, OBS_HUNT, OBS_NOJOB, OBS_CAST_FAR, OBS_POOL_SMALL };
struct neth_vec_bp_trace {
    int32_t obs, keep, bucket, water, lava, flint, dpick, cobble, dirt, furnace, kind;
    int32_t bp_want, bp_start, bp_mode_next, bp_on, bp_mode, bp_site, bp_step, bp_mem_w, bp_mem_l, bp_pick_w, bp_pick_l;
    int32_t ob_want, ob_on, ob_job, ob_who, ob_search, ob_mem_w, ob_mem_l;
    int32_t pp_want, pp_on, pp_step;
    int64_t bp_off, ob_off, pp_retry;
    /* the provisions (food_oracle's prov_want, food.c): the food points held, the want, the scout on and its count, its
     * rest's end, the hunt's lock on, the census's food animals near (PRIVILEGED, as the scout reads them: -1 no census)
     * and the nearest's distance across and its feet under the sky */
    int32_t food_pts, prov, fs_on, fs_n, hunt, food_n, food_sky;
    int64_t fs_off;
    float food_d;
    /* ob_start's last answer (OBS_*, -1 never asked) and its step */
    int32_t ob_why;
    int64_t ob_why_step;
    /* the hunt's want now (food.c hunt_want: the food animal in view's vis index, -1 none or not wanted), its rest's end
     * (hunt_off), the meats held and the hunt's cap */
    int32_t hunt_vis, meat, hunt_cap;
    int64_t hunt_off;
    /* provision_mode's answer (PV_*: 0 off, 1 the surface stage, 2 the stock stage), food_oracle's walk to the census's
     * animal (fo_wt: a path held, its cells), the hunted animal's last place in view across (-1 no hunt) */
    int32_t pv, fo_have, fo_n;
    float hunt_d;
    /* the strip's climb's want now (strip2.c st3_want: 0 none, else ST3_*; with the provisions wanted the strip's own
     * want is 0), strip_want's two vetoes (shore_night_yield, obsidian_yield's e->ob.yield), its rest's end (soff),
     * whether a strip runs (e->strip) */
    int32_t st3, st_night, st_yield, st_on;
    int64_t soff;
    /* the hunt's lock (fight.c fight_core): the hunted animal in view (1, 0 not, -1 no hunt), the steps walked after it
     * out of view (flost), the drop search's step (hpk, 0 not begun), its kill seen (the kill stat past the hunt's
     * start), the lock's age, the gstuck count */
    int32_t hk_vis, flost, hpk, hkilled, hage, gstuck;
    /* the env's own step count (the cooldowns' clock: hunt_off, fs_off, soff), scout_want (-1 with the census off),
     * the hunted animal's kind while in view (AK_*, -1 otherwise) */
    int64_t esteps;
    int32_t sw, hkind;
    /* the cook's books: raw meat held (craft.c raw_meat), coal, planks, and the craft plan craft_plan would choose now
     * (C_*, -1 none) */
    int32_t raw, coal, planks, cplan_next;
    /* the screen (PS_*), and with the furnace's open its input, fuel and output slots (item, count; -1 otherwise), the
     * craft macro's plan and phase while it runs (-1 otherwise) */
    int32_t screen, fin, fin_n, ffuel, ffuel_n, fout, fout_n, cplan, cphase;
    /* tables held, the env's has_table (one placed), a table and a furnace at hand (craft.c near_table, near_furnace) */
    int32_t tables, has_table, near_tb, near_fu;
    /* scout_want's other tests: the census's sky over the player (-1 with the census off), on the ground, the eye in
     * water */
    int32_t pl_sky, on_ground, eye_water;
    /* the obsidian macro's remembered lava (ob_mem_pick's cast list) by the first test that refuses each: given up
     * (ob_bad), at or over the feet's height, beside the feet's column (the rest of ob_lava_far), OB_MEM_GO or farther;
     * those it may take; the nearest cell's distance and height from the feet (-1, 0 none); obsidian_strip_cast's
     * flag */
    int32_t obm_n, obm_bad, obm_up, obm_adj, obm_far, obm_ok, obm_dy, ob_scs;
    float obm_d;
    /* the same cells in the client world now: lava sources (metadata 0), flowing lava, no lava there any more; the
     * nearest source's distance (-1 none) */
    int32_t obm_src, obm_flow, obm_gone;
    float obm_srcd;
};
int neth_vec_bp_trace(const struct neth_vec *v, int id, struct neth_vec_bp_trace *out);
/* lane/proom (chain --death-trace, read only): provision_mode's answer now (PV_*: 0 off, else the provision controller
 * takes the step before any macro's test; its steps are labelled mbit 0, as the policy's are), -1 a bad id or no chain */
int neth_vec_prov_mode(const struct neth_vec *v, int id);

/* lane/chain (neth_vec_opts.chain 1; every call -1 without it). Env id's last result as the step hook saw it (read
 * between recv and send of its group; after a recv that reset it, the reset step's): the player's place, look, health
 * and food, the flags (pool.h PF_*), the macro kind running after it (0 none), the milestones held (the episode's and
 * those carried from its start: bit i reward.MILESTONES[i], NETH_VEC_MS_END, NETH_VEC_MS_DRAGON), the dimensions
 * been in (bit 0 the Nether, 1 the overworld, 2 the End; carried too), the episode's ticks, and the state */
struct neth_vec_chain_peek {
    double x, y, z;
    float yaw, pitch, health, saturation;
    int32_t dim, food, air, fire, flags, kind, dims, reset_step;
    uint32_t ms;
    int32_t pad_;
    int64_t world_time, te;
    struct neth_rl_state st;
};
int neth_vec_chain_peek(const struct neth_vec *v, int id, struct neth_vec_chain_peek *out);
/* lane/wood: the run file's seen_log2 on (rlbind.c's cast: the seen log either log block); and env id's wood_search
 * books (mods/macros/wood.c: seeks begun, turns on the spot, heading turns, steps driven), 0 or -1 */
int neth_vec_seen_log2(const struct neth_vec *v);
int neth_vec_wood(const struct neth_vec *v, int id, int64_t out[4]);
/* The stronghold controller's located portal-room centre, once known: 0 found, 1 not yet located, -1 bad env.
 * A search may rank its walk against this same room rather than a room chosen earlier from spawn. */
int neth_vec_chain_room_target(const struct neth_vec *v, int id, double *x, double *z);
/* env id (idle: between recv and send) saved as a start (rlbind.h neth_rl_start_save: made in the background), with the
 * vector env's memory of the episode so far: its index, or -1 with err (try again later when the save slots are busy) */
int neth_vec_chain_save(struct neth_vec *v, int id, char *err, int en);
/* The version-3 sidecar header. Its payload follows immediately, in piece order. Even-numbered pieces 2 through
 * 12 are the Nether, armor, stronghold, pearl, obsidian and bucket-portal controllers; fixed gaps are exact-size.
 * New fields go past each controller's old physical end, including its padding, and default to zero. */
enum { NETH_RL_CHAIN_MEM_MAGIC = 0x43484d31, NETH_RL_CHAIN_MEM_VERSION = 3,
       NETH_RL_CHAIN_MEM_PIECES = 14, NETH_RL_CHAIN_MEM_PEARL = 8 };
struct neth_rl_chain_mem_header {
    uint32_t magic, version, bytes, state_bytes, pieces;
    uint32_t piece_bytes[NETH_RL_CHAIN_MEM_PIECES];
};
/* The macro memory beside a search node's on-disk segment. A missing sidecar is an old node and returns 1. */
int neth_vec_chain_memory_write(const struct neth_vec *v, int start, const char *path, char *err, int en);
int neth_vec_chain_memory_read(struct neth_vec *v, int id, const char *path, char *err, int en);
/* lane/fwd (chain search --crossings): env id's macro memory now, raw (the live entity references kept), to path, and
 * read back into env id: for a restore in the same build whose world its own acts rebuilt; 0, -1 with err */
int neth_vec_chain_memory_dump(const struct neth_vec *v, int id, const char *path, char *err, int en);
int neth_vec_chain_memory_load(struct neth_vec *v, int id, const char *path, char *err, int en);
/* A seam check forgets only the in-process controller for a saved start, forcing a restore from its disk sidecar. */
int neth_vec_chain_memory_forget(struct neth_vec *v, int start);
/* The disk-seam gate reads selected controller clocks and phases, including ones whose next act is unchanged. */
struct neth_vec_chain_controller_probe {
    int64_t steps, stronghold_wait_at;
    int portal_step, portal_x, portal_y, portal_z;
    int pearl_phase, food_trip, strip_phase, dragon_phase;
};
int neth_vec_chain_controller_probe(const struct neth_vec *v, int id,
                                    struct neth_vec_chain_controller_probe *out);
/* Exact canonical controller bytes for a disk-seam check. Excluded: recorder/playback pointers, runtime queues,
 * cached pearl and Nether entity views, and current fight, pearl, eye and crystal target entity IDs. */
size_t neth_vec_chain_controller_bytes(const struct neth_vec *v, int id, void *out, size_t cap);
/* env id's next episode (from the next reset: its clock's end or neth_vec_chain_end) from start (a loaded or saved
 * start's index; -1: drawn as without chain) for ep_len ticks (0: the make's ep_len); 0, -1 */
int neth_vec_chain_next(struct neth_vec *v, int id, int start, int64_t ep_len);
/* env id's episode ends with its next step (the recv after it resets it to neth_vec_chain_next's start) */
int neth_vec_chain_end(struct neth_vec *v, int id);
/* env id's next n decisions decoded with no macro but the screen's and the respawn (the policy's action alone) */
int neth_vec_chain_free(struct neth_vec *v, int id, int n);
/* env id's next n decisions are acts[0..n) (the caller's memory, kept until they are played), no macro: a found chain
 * played again into an env (the search's resume: the state then saved) */
int neth_vec_chain_play(struct neth_vec *v, int id, const struct neth_rl_act *acts, int64_t n);

/* rlbind.c's side: the binding hands its worker hooks to the vector env */
struct pipe;
struct pool_act;
struct pool_result;
void neth_vec_attach(struct nwrl *h, struct neth_vec *v);
struct pipe *neth_rl_pipe(struct nwrl *h);
int neth_vec_act_hook(struct neth_vec *v, int id, int k, const struct neth_rl_state *st, struct pool_act *out);
void neth_vec_step_hook(struct neth_vec *v, int id, const struct pool_result *r, const struct neth_rl_state *st);

#endif
