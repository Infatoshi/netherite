/* the macros' run-file options (policy.actions NAME: WORD; lane/macroopts' MOPTS, lane/macromod: cut from vecenv.c, unchanged) */

/* lane/macroopts (Elliot, 2026-11-02): every either/or a macro had hard-coded (a tactic, a variant a lane measured
 * against another, a compile-time -D it left) is a run-file option, policy.actions.NAME: WORD. The default is today's
 * behaviour: "easiest" is easiest for the policy that is distilled from the macro (the orchestrator, 2026-11-02), the
 * way that reaches the step most often, not the simplest code. One exception: lava_sense's default is view, since
 * today's near rested on lava's sound (Elliot: no macro uses sound). Words are listed in the order of their values;
 * tuning constants stay named at the top of each macro's file */
static const struct mopt {
    const char *name, *words;
    int def;
    const char *what;
} MOPTS[MO_N] = {
    [MO_SH_LOCATE] = {"stronghold_locate", "follow,pair,snap", SHL_SNAP,
                      "follow: each eye's heading walked along and thrown again (a reversal halves the stride) until "
                      "one flies down; pair: two throws 24 blocks apart crossed (triangulation); snap: one far throw's "
                      "heading read onto the ring's chunk grid, the pair its fallback (lane/stronghold2)"},
    [MO_LAVA_SENSE] = {"lava_sense", "view,light,near", LS_VIEW,
                       "obsidian, bportal and nether's lava in the client world: view, only a cell the eye has a clear line to; "
                       "light, also a cell whose glow reaches an open cell the eye sees; near, every cell round the "
                       "player and the target (the light-and-sound rule before lane/macroopts)"},
    [MO_NT_FAN] = {"nether_fan", "off,on", 1, "the far view's fan of rays (off: lane/nether2's lava veto alone)"},
    [MO_NT_GOALS] = {"nether_goals", "off,on", 0, "the search by goals the far view saw (lane/nether3's first try) "
                                                  "instead of the legs"},
    [MO_NT_ANY_BLAZE] = {"nether_any_blaze", "off,on", 0, "a blaze fight strikes any blaze under the crosshair"},
    [MO_NT_NEAR_STOP] = {"nether_near_stop", "off,on", 0, "a blaze in view nearer than 4 blocks: no hood begun, no "
                                                          "glance"},
    [MO_NT_COVER] = {"nether_cover", "off,on", 1, "a far blaze in view walled off"},
    [MO_NT_HOOD] = {"nether_hood", "off,on", 1, "the hood built at the spawner when hurt (off: the retreat)"},
    [MO_NT_GLANCE] = {"nether_glance", "off,on", 1, "the glance round between steps (lane/nether4)"},
    [MO_NT_FOOTING] = {"nether_footing", "off,on", 1, "the inventory opened only with footing under the whole box"},
    [MO_NT_BRICK_PICK] = {"nether_brick_pick", "off,on", 1, "nether brick dug with the best pickaxe (off: by hand)"},
    [MO_NT_PICK_FETCH] = {"nether_pick_fetch", "off,on", 1, "the best pickaxe swapped into the hotbar when it holds "
                                                            "none"},
    [MO_NT_FORT_STAIR] = {"nether_fort_stair", "off,on", 1, "at the fortress a stair whose cell is its own brick walked "
                                                            "level instead (off: that way judged bad; lane/nether5)"},
    [MO_NT_FORT_REJOIN] = {"nether_fort_rejoin", "off,on", 1, "fallen off the fortress to under its height, a pillar "
                                                              "back up (off: the search goes on down there; lane/nether5)"},
    [MO_NT_SKEL_FIGHT] = {"nether_skeleton", "off,on", 1, "a skeleton in view within 16 blocks fought, closed on "
                                                          "crouched, and a hit with nothing in view turns the glance "
                                                          "(off: skeletons fought within 4; lane/nether5)"},
    [MO_NT_FAR_SPAWNER] = {"nether_far_spawner", "off,on", 1, "a fortress seen, the spawner looked for in view out to "
                                                              "40 blocks (off: the 20-block scan and the fan's rays; "
                                                              "lane/nether5)"},
    [MO_NT_BLAZE_FAR] = {"nether_blaze_far", "off,on", 0, "searching (no spawner or camp yet), a blaze in view within "
                                                          "12 blocks fought and closed on (off: within 6, as at the "
                                                          "spawner; lane/nether5: on, gold-g7's 24 lava deaths became "
                                                          "24 blaze deaths, no rod)"},
    [MO_NT_FRONTIER] = {"nether_frontier", "off,on", 1, "in the fortress, the floors the far view saw remembered and "
                                                        "the nearest not walked near gone to when none is in view (off: "
                                                        "a dug leg; lane/nether5)"},
    [MO_NT_HOOD_ANY] = {"nether_hood_any", "off,on", 1, "hurt anywhere on the fortress, the hood (off: only at a "
                                                        "spawner or camp, the rest crouched in the open elsewhere; "
                                                        "lane/nether5)"},
    [MO_NT_FORT_UP] = {"nether_fort_up", "off,on", 1, "in the fortress, a floor the far view sees over the feet scored "
                                                      "up by its rise (off: any height change scored down; "
                                                      "lane/nether5)"},
    [MO_NT_SHY] = {"nether_shy", "off,on", 1, "searching the fortress, a blaze in view out of the fight's reach: back "
                                              "along the way walked until it is out of view, then on (off: the walk "
                                              "goes on in its view; lane/nether5)"},
    [MO_NT_LAVA_FLOOR] = {"nether_lava_floor", "off,on", 1, "in the fortress, lava seen lying at the deck's height (a "
                                                            "lavafall spread over a bridge) a goal after the dry floors, "
                                                            "bridged over with cobblestone (off: never a goal; "
                                                            "lane/nether5)"},
    [MO_NT_COVER_SEARCH] = {"nether_cover_search", "off,on", 0, "searching the fortress (no spawner or camp yet), a far "
                                                                "blaze walled off as at the spawner (off: with "
                                                                "nether_shy on, the shy backs off instead; "
                                                                "lane/nether5)"},
    [MO_NT_SKEL_TOWER] = {"nether_skel_tower", "off,on,far", 1, "a wither skeleton within 10 blocks: a pillar 3 over its "
                                                            "feet (out of its reach as it jumps), the fight from the "
                                                            "top, then the pillar dug back down (off: fought on the "
                                                            "floor; lane/nether5); far, the same begun only with the "
                                                            "nearest beyond 4.5 blocks and its pillar dropped when the "
                                                            "player is hit on it (its knockback keeps the pillar from "
                                                            "ever placing: gold-g7 with nether_blaze_seek deck, 32 of 32 "
                                                            "killed on one; lane/rod)"},
    [MO_NT_CAMP_GIVEUP] = {"nether_camp_giveup", "off,on", 1, "a camp with no spawner (where a natural blaze died) "
                                                              "given up after 2400 steps with no kill, the search "
                                                              "again (off: camped to the end; lane/nether5)"},
    [MO_OB_DEEP] = {"obsidian_deep", "off,on", 0, "the search at the lava layer's depth (lane/obsidian's variant)"},
    [MO_OB_EARLY_FILL] = {"obsidian_early_fill", "off,on", 0, "an empty bucket filled at water in view as soon as it "
                                                              "is made (lane/obsid: the strip hands the step over when water "
                                                              "is in view, and the best pickaxe goes back in hand after)"},
    [MO_OB_DESCEND] = {"obsidian_descend", "off,on,far", 1,
                       "a stair down toward a cast's lava: on, within 7 blocks across (10 with obsidian_cast_way strip); "
                       "far, that reach plus the drop, the stair ending as near (lane/obsid)"},
    [MO_OB_CAST_SOURCE] = {"obsidian_cast_source", "off,on", 0, "the cast only where the ray lands on a lava source"},
    [MO_OB_LOOK] = {"obsidian_look", "off,on", 1, "the look around every few blocks walked"},
    [MO_OB_SEARCH] = {"obsidian_search", "off,on", 1, "the strip search (off: only what the policy's walk shows)"},
    [MO_OB_FLINT] = {"obsidian_flint", "first,later", 0,
                     "the obsidian macro's kit: first, flint and steel held before it starts (lane/obsidian's); "
                     "later, a diamond pickaxe and a bucket alone (the obsidian mined first, the flint and steel "
                     "only lights the frame; lane/obsidian7: the library's seed-13 diamond pickaxe start holds none)"},
    [MO_OB_WATER] = {"obsidian_water", "search,sky,scout", 1,   /* lane/obsid: sky the default (with the trip) */
                     "an empty bucket, no water in view or remembered: search, the strip search finds it (lane/"
                     "obsidian2's); sky, the climb goes up a stair at a time until water is in view or the head is "
                     "under the sky, then the look around (lakes, rivers and seas lie on the surface; lane/obsidian7: "
                     "the library's diamond pickaxe starts' search saw no water in 16,000 to 20,000 steps at y 15); "
                     "scout, the trip and then, under the sky by day, a walk in a square spiral with a far look (64 "
                     "blocks) every 16 blocks until water is seen, walked to and filled (lane/obsid: 3 of the 26 "
                     "from-spawn diamond pickaxe runs ever saw water with an empty bucket before the pickaxe)"},
    [MO_OB_DROP] = {"obsidian_drop", "off,on", 0,
                    "the search's strip with no floor ahead: off, a quarter turn (four, the strip rests 400 steps); on, "
                    "a second look for a floor up to OB_DROP_DEEP blocks lower in that column, no liquid over it, walked "
                    "off onto (lane/obsidian7: s42's start stood on a cave's pillar at y 42 for 20,000 steps)"},
    [MO_OB_CAST_WAY] = {"obsidian_cast_way", "walk,strip", 0,
                        "a water bucket and lava (in view or remembered) farther across than the stair down takes: walk, "
                        "the cast's crouched walk (lane/obsidian2's); strip, the search's strip tunnels toward it at its "
                        "height and the cast begins within the stair's reach (lane/obsidian7: s42's casts at y 15 gave up "
                        "on lava at y 10, 20 to 35 blocks across, the walk held by the tunnel's wall)"},
    [MO_OB_FOOD] = {"obsidian_food", "off,hunt", 0,
                    "the obsidian macro and a food animal in view, the food level at 16 or under: off, the macro starts "
                    "(the hunt is tried on every fifth step between macros); hunt, it does not, the hunt starts "
                    "(lane/obsidian7: every env of s42's idle case died with the food level at 0)"},
    [MO_OB_CAST_RIM] = {"obsidian_cast_rim", "off,on", 1,   /* lane/obsid: on the default (the unit case's pool) */
                        "the cast's pour: off, into the lava's own cell (the four beside it hardened); on, on the top of a "
                        "solid block beside the lava where one is open over it, the water running a block over the pool "
                        "and hardening each source it covers, 60 steps still after (lane/obsidian7: the obsidian a cast "
                        "made in s42's case lay over more lava, never dug)"},
    [MO_OB_SEA] = {"obsidian_sea", "turn,stop", 0,
                   "the water trip's climb under sand, gravel or clay (a sea's or a lake's floor): turn, a quarter turn and "
                   "on; stop, the trip over, none again for OB_SKY_BED steps (lane/obsidian7: s13's turn dug under the same "
                   "gravel and the sea came in, every env drowned)"},
    [MO_SURF_CEIL] = {"surface_ceiling", "off,dig", 0,
                      "the surface macro's swim up under a solid block within reach over the head: off, it jumps on "
                      "(the swim's); dig, the block dug with the best tool (lane/obsidian7: a flooded pit under a grass "
                      "ceiling drowned every env of s42's diamond pickaxe start)"},
    [MO_DG_LOB] = {"dragon_lob", "off,on", 1, "a crystal hidden on its pillar lobbed at (off: lane/dragon2's hunt)"},
    [MO_DG_CRIT] = {"dragon_crit", "off,on", 1, "the sword's swing on a jump's way down"},
    [MO_DG_BLOCK] = {"dragon_block", "off,on", 1, "the sword held blocking between swings"},
    [MO_PEARL_ZOMBIE_PILLAR] = {"pearl_zombie_pillar", "off,on", 1, "zombies fought from the pillar (off: "
                                                                    "lane/pearl2's ground fight)"},
    [MO_PEARL_DROP_CALM] = {"pearl_drop_calm", "off,on", 1, "the drops' quiet counts only screaming endermen within "
                                                            "32"},
    /* lane/pearlgate: the pearl step's gate (mods/macros/pearl.c pearl_gate; default nether: the night alone made every
     * night of every run the enderman hunt, lane/brutetab and lane/stagelen) */
    [MO_PEARL_WHEN] = {"pearl_when", "night,nether,powder", 1,
                       "the pearl hunt's start, with pearls and eyes under 12 in the overworld at night: night, any "
                       "night (lane/pearl's); nether, a blaze rod or blaze powder held (the Nether done); powder, the "
                       "eyes short covered by the blaze held (2 a rod, 1 a powder, 1 an eye: 12)"},
    [MO_PEARL_YIELD] = {"pearl_yield", "off,on", 1,
                        "the hunt not begun while a step is mid-way: a bucket portal's frame cast, the stronghold's "
                        "search, the strip's climb out of the depth, a fight in the last 100 steps, the late macro's "
                        "blaze powder or eye of ender to craft"},
    [MO_BP_ROAM] = {"bportal_roam", "off,surface,pillar", 1,
                    "lava wanted and none known: off, the strip search under ground (lane/obsidian4); surface, a "
                    "climb to the sky, then a walk in a square spiral over the surface with a far look every few "
                    "blocks (lakes and ravines open to the sky in view), the strip after its steps run out; pillar, "
                    "the same with each far look from a pillar of blocks put under the feet, dug back down after "
                    "(lane/obsidian5)"},
    [MO_BP_NIGHT] = {"bportal_night", "walk,strip", 1,
                     "the walk over the surface at night (the sky's time 13000 to 23000): walk, as by day; strip, it "
                     "yields and the strip search runs under ground, the climb back up by day (lane/obsidian5: the kit "
                     "cases on seeds 393 and 258, 64 envs each, deaths 49 and 92 under walk, 45 and 61 under strip, the "
                     "first under the macro 11 and 24 against 4 and 11, the lights the same)"},
    [MO_BP_BUCKETS] = {"bportal_buckets", "1,2,any", BB_TWO,  /* orchestrator: 2 by default (any: the Nether 0 of
                                                                * 256 from spawn either way, diamond pickaxes 9 against
                                                                * 19; lane/obsidian6's eval) */
                       "the buckets the bucket portal starts with and casts by: 1, one or more, every cell cast the "
                       "one-bucket way (the water poured beside the cell before the lava is fetched, picked back up "
                       "after); 2, two or more, the water carried in one bucket and the lava in the other "
                       "(lane/obsidian3 to 5); any, one or more, the one-bucket way while one is held "
                       "(lane/obsidian6)"},
    [MO_SH_NIGHT] = {"stronghold_night", "walk,hide,hurt", SHN_HIDE,
                     "the stronghold's walk at night: walk, on through it (lane/stronghold2); hide, a hole 3 deep "
                     "with a block over the head from dusk to the morning, on the surface's phases; hurt, the walk on "
                     "and the hole only at 10 health or under, left healed to 18 (lane/stronghold3)"},
    [MO_SH_ROOF] = {"stronghold_roof", "cave,sky", SHR_SKY,
                    "the staircase up from under a roof: cave, only under y 50 while walking (lane/stronghold); sky, "
                    "at any height before a throw or the walk, the pickaxe spent on the way up (lane/stronghold3)"},
    [MO_SH_SPRINT] = {"stronghold_sprint", "always,fed", SHS_FED,
                      "the stronghold's walk sprints: always, with food over 6 (lane/stronghold); fed, also only while "
                      "8 meats are held (lane/stronghold3)"},
    [MO_SH_WALL] = {"stronghold_wall", "detour,pillar", SHW_PILLAR,
                    "the stronghold's walk at stone with no pickaxe to spare: detour, turned aside (lane/stronghold); "
                    "pillar, up it on cobblestone, two blocks a time (lane/stronghold3)"},
    [MO_SH_TUNNEL] = {"stronghold_tunnel", "turn,straight", SHT_STRAIGHT,
                      "the stronghold's walk after a tunnel that broke a block: turn, the stall still counted (every "
                      "third a detour aside; lane/stronghold2); straight, the stall streak begun again, the tunnel on "
                      "toward the goal (lane/stronghold3)"},
    [MO_SH_THROW_KEY] = {"stronghold_throw_key", "policy,macro", SHK_MACRO,
                         "the policy's throw key while the stronghold macro holds the eyes: policy, an eye thrown "
                         "(lane/throweye); macro, ignored, the macro's own throws only (lane/stronghold3)"},
    [MO_SH_ROOM_WALK] = {"stronghold_room_walk", "off,outside", SHM_OUTSIDE,
                         "a frame out of reach from the portal room's floor: off, left for now (lane/stronghold2); "
                         "outside, walked to on the floor past its outer side, where its top is in reach "
                         "(lane/stronghold3)"},
    [MO_SH_NIGHT_NEAR] = {"stronghold_night_near", "off,on", SHNN_ON,
                          "stronghold_night hide's hole skipped with the located goal (the start's chunk, a near "
                          "throw, the dig) within 64 blocks: off, hidden there too; on, on to the dig "
                          "(lane/stronghold3)"},
    [MO_SH_STUCK] = {"stronghold_stuck", "off,turn", SHU_TURN,
                     "the stronghold's walk no nearer its goal for 600 steps: off, its own stall rules only; turn, a "
                     "long detour square to the way, sides taken in turn (lane/stronghold3)"},
    [MO_SH_HUNT] = {"stronghold_hunt", "off,low", SHH2_LOW,
                    "the food macro's hunt while the stronghold macro walks: off, only between its yields; low, an "
                    "animal in view within 16 by day with fewer than 4 meats held (lane/stronghold3)"},
    [MO_SH_FLESH] = {"stronghold_flesh", "off,starving", SHF_STARVING,
                     "rotten flesh while the stronghold macro is latched: off, never eaten (the food macro's meats "
                     "leave it out); starving, eaten at food 4 or under with no meat held (lane/stronghold3)"},
    [MO_BP_DPICK] = {"bportal_dpick", "obsidian,bportal,always", 0,
                     "one bucket and a diamond pickaxe held, flint and steel too, both macros on: obsidian, the "
                     "obsidian macro mines it (lane/obsidian6's split); bportal, the bucket portal casts the frame "
                     "the one-bucket way (bportal_buckets 1 or any) and the obsidian macro stands aside (lane/obsidian7); "
                     "always, the obsidian macro with two buckets too (lane/obsid)"},
    /* lane/ladder2: the mine lock's tool pick alone, for a policy without the mine lock (the L2 student): the
     * measurement of how much of the L1-to-L2 drop is the slot */
    [MO_POLICY_TOOL] = {"policy_tool", "off,on", 0,
                        "on: on a step the policy drives (no macro), its attack held on a block and no number key of "
                        "its own, the hotbar's best tool for the block (the mine lock's best_tool) is selected that "
                        "tick; off, the slot is the policy's alone"},
    /* lane/ladder2: the mine lock without its tool pick, the other half of that measurement (the lock's aim and its
     * held attack alone) */
    [MO_MINE_TOOL] = {"mine_tool", "off,on", 1,
                      "on: the mine lock (the policy's attack on a block) selects the hotbar's best tool for the block; "
                      "off, it aims and holds the attack with the slot the policy left (the strip, seek and the other "
                      "macros pick their tools as before)"},
    /* lane/ladder4: the mine lock's unbroken attack alone, for a policy without the mine lock (the L2 student): in
     * 1.7.10 one tick without attack drops the block's damage to 0, so a sampled key breaks a long dig */
    [MO_POLICY_HOLD] = {"policy_hold", "off,on", 0,
                        "on: on a step the policy drives (no macro), while the client hits the block under the "
                        "crosshair (a dig under way: its crack and particles on the screen), the attack stays held "
                        "where the policy let it go; off, the attack is the policy's alone"},
    /* lane/ladder5: the seek's reach with the strip on. The teacher (every macro) seeks ore within 8 blocks only while
     * the strip is in its set, the L2 student (no strip) at any distance in view, so no label of the teacher's shows
     * the walk to far ore a student without the seek (L3) must make itself */
    [MO_SEEK_REACH] = {"seek_reach", "strip,any", 0,
                       "strip: with NETH_VEC_STRIP in the set the seek takes iron ore, diamond ore and gravel within 8 "
                       "blocks of the eye only (lane/strip: ore across a cave stopped the tunnel); any, at any distance "
                       "in view, as without the strip"},
    /* lane/ladder10: GUI clicks as the policy's own action, for a policy without the craft or screen macro */
    [MO_GUI] = {"gui", "off,cursor", GUI_OFF,
                "off: the policy's action cannot click (a macro's GUI ops are labelled unsayable); cursor: under a "
                "container screen the policy's camera bins are the mouse on the cursor grid (vecenv.h "
                "NETH_VEC_GUI_B0) and its keys click (mine left, place right, sneak with mine shift, the hotbar element "
                "a number key's swap), key 12 the inventory key (opens the inventory, closes a screen), and a macro "
                "emits one GUI op a step (the rest queued), so every op is a step the 16-wide action can say"},
    /* lane/ladder10: the craft macro's table placing (place_item's slot) alone, for a policy without the craft macro: the
     * measurement of how much of L4b's gap is the table's slot */
    [MO_POLICY_PLACE] = {"policy_place", "off,on", 0,
                         "on: on a step the policy drives (no macro), its use pressed with the crosshair on a block whose "
                         "use opens no screen and a crafting table in the hotbar, the table's slot is selected that tick "
                         "(policy_tool's kind of aid, for placing); off, the slot is the policy's alone"},
    /* lane/ladder11: a macro's press of use or attack (the key down for one tick, not held: the craft macro placing the
     * table, opening it and the furnace) was labelled with the key off and NETH_VEC_LBL_PRESS set, which the distiller
     * does not train on, so a student without the craft macro was told to let go of use on every step the macro placed
     * or opened the crafting table */
    [MO_PRESS_LABEL] = {"press_label", "flag,key,slot", PRL_FLAG,
                        "flag: a macro's press of use or attack that its holds do not make is labelled with the key off "
                        "(NETH_VEC_LBL_PRESS set); key: as the key held that step (the policy's key held one step "
                        "presses it on its down step, as the macro did); slot (lane/ladder13): key's, and a macro's use "
                        "press with a crafting table or furnace selected (placing it) also labels the hotbar with that "
                        "slot (policy_place's choice, the student's to make on its own use press)"},
    /* lane/ladder15: the craft macro starts on every fifth step, so at the teacher's macros a crafting plan's state is
     * labelled NETH_VEC_TABLEKEY's key once (the start, label half 4) and the teacher's own act on the steps before it;
     * L4gC-s1 at 35M started the table half 4.9 times a run with 7,400 steps a run of no screen and a crafting plan
     * named */
    [MO_TABLE_CHANCE] = {"table_chance", "off,on", 0,
                         "on: a step the policy drives (no macro) with no screen open while craft_plan names a crafting "
                         "plan (a 2x2 or 3x3 recipe or the smelt: where the craft macro starts on its cadence) is "
                         "labelled half 5 (the table key's chance; distill.table_label's key 12); off, half 0"},
    /* lane/ladder15: key 12 is also the inventory key, and a press NETH_VEC_TABLEKEY does not take (no crafting plan
     * named) opens the inventory: L4gC-s1's kept student opened its own inventory 12.3 times a run (4.1 table halves by
     * the key) and sat under it 11,549 steps a run with no plan */
    [MO_TABLE_KEY] = {"table_key", "inventory,paused", TK_INVENTORY,
                      "NETH_VEC_TABLEKEY's key: inventory, key 12 (the inventory key: outside a crafting plan's state "
                      "it opens the inventory); paused, key 13 (the action's last key, read by nothing else: no meaning "
                      "in or outside a screen; the distiller clears the teacher's own key 13 and labels the table "
                      "half's first step with it)"},
    /* lane/ladder16: L5aK-s1's kept student (NETH_VEC_SEEKKEY, the seek off) had the seek's start holding on 434 of a
     * run's policy steps (eval seed 7) and pressed key 13 there 0.91 times a run: at the teacher's macros the seek
     * starts within five steps of its start holding, and on the student's own L5a episodes those steps are masked */
    [MO_SEEK_CHANCE] = {"seek_chance", "off,on", 0,
                        "on: at a level with NETH_VEC_SEEKKEY and the seek off, a step the policy drives (no macro, no "
                        "screen, alive) where the seek's start holds (seek_want) is labelled half 7 (the seek key's "
                        "chance; distill.seek_label's key 13); off, half 0"},
    /* lane/ladder18: the seek's execution in two halves, each the policy's at a level with NETH_VEC_SEEKKEY (the int32
     * macros have no bit left, so these are options; the teacher's macros, without SEEKKEY, keep the whole seek). The
     * walk half: the seek's steps with its cell farther than SEEK_DIG_REACH from the eye and nothing dug yet (the walk
     * and any block dug in the line on the way); the dig half: the cell within it, or dug (the dig, the walk to the
     * drop and its pickup). With the label halves (vecenv.h neth_vec_label.half 6 and 8) the options also say, at any
     * macros, which seek steps are the key's start and which the policy's own */
    [MO_SEEK_WALK] = {"seek_walk", "macro,policy", 0,
                      "policy: at a SEEKKEY level the seek's walk is the policy's: the seek starts (key 13) only on a "
                      "cell within SEEK_DIG_REACH (4 blocks, eye to the cell's centre) and ends, handing back, when its "
                      "cell is farther with nothing dug; a seek step's label is half 8 (the policy's) in the walk half, "
                      "half 6 (the key) on the dig half's first step; macro, the whole seek as before"},
    [MO_SEEK_DIG] = {"seek_dig", "macro,policy", 0,
                     "policy: at a SEEKKEY level the seek's dig is the policy's: the seek starts only on a cell farther "
                     "than SEEK_DIG_REACH and ends, handing back, when its cell comes within it; a seek step's label is "
                     "half 8 in the dig half, half 6 on the walk half's first step; macro, the whole seek as before"},
    /* lane/ladder17: the first execution rung (L6a): ladder10's L4bE student click-crafted the whole wooden pickaxe kit
     * (planks, sticks, the table) through gui cursor in 190 of 256 runs with no macro at all */
    [MO_RECIPE_2X2] = {"recipe_2x2", "key,clicks", R2_KEY,
                       "the 2x2 recipes (a plan craft_plan makes in the inventory's grid: planks, sticks, the table, "
                       "the torch, flint and steel, blaze powder, eyes) at an env with NETH_VEC_NORECIPE: key, the "
                       "recipe half as the level says (NETH_VEC_RECIPEKEY's jump starts it); clicks, never the macro's: "
                       "under the inventory the policy's own clicks craft them (gui cursor), the 3x3 recipes and the "
                       "smelt as before. With clicks the craft macro's 2x2 recipe steps (its clicks and its close, at "
                       "any macros) are label half 9 (the distiller's level, NETH_VEC_NORECIPE, trains them as the "
                       "macro's acts); NETH_VEC_LBL_FITS is not set for a 2x2 plan; and at the student's level "
                       "(neth_vec_level) the 2x2 labeller (x2_label) labels the policy's own inventory steps"},
    /* lane/ladder17: at L6a-s1's 69M checkpoint the labeller's runs were left 160 of 164 times a run, 120 of them at
     * the first op and 139 by a click on another slot: recipe_ops picks the first stack of the part (hotbar first),
     * and the student's tape picked the logs from another hotbar slot; off the run every later step says the close */
    [MO_X2_SOURCE] = {"x2_source", "first,any", X2S_FIRST,
                      "the 2x2 labeller's part stack (recipe_2x2 clicks): first, the craft macro's own (recipe_ops: the "
                      "first inventory stack of the part), any other click leaves the run; any, a plain left click on "
                      "another main inventory or hotbar slot holding the same item where the label picks the part up "
                      "(the cursor empty, enough for the grid cells that follow) or puts the rest back (empty or the "
                      "same item) follows the run, its later ops on that slot moved to the clicked one"},
    /* lane/obsid: the from-spawn eval's smelt loop (5 of the 14 diamond pickaxe runs that timed out, 29% of their steps) */
    [MO_PLACE_DIG] = {"place_dig", "any,skip", 1,
                      "the craft macro's placing (a table or furnace) with no floor in any of the four looks digs a "
                      "side cell for room: any, whatever is there (a crafting table there is dug by hand for 60 steps "
                      "and stands, the plan ends and starts again 5 steps later, a thousand times a run); skip, a "
                      "table, furnace or chest there is passed over for the next side (lane/obsid)"},
    [MO_OB_TRIP] = {"obsidian_water_trip", "off,on,bucket", 1,
                    "on: the surface water source seen nearest where the player is kept (the memory's look, at y 56 or "
                    "over); an empty bucket held, no water bucket, the head under the sky by day: the walk to it "
                    "(upright, within 128 across and 8 up or down) and the fill, before the diamond pickaxe too (no "
                    "flint and steel needed for it); every diamond pickaxe run from spawn had seen water before its "
                    "bucket, 4 of 63 held water at the pickaxe (on: 15 of 57, the trip begun in 10, 42 of the buckets "
                    "made under y 40); bucket: also the climb to the sky for it as the empty bucket is held, the "
                    "diamond pickaxe not yet, by day (before 9000), at most twice (lane/obsid)"},
    [MO_OB_STRIP_CAST] = {"obsidian_strip_cast", "off,on", 1,
                          "on: the diamond pickaxe and a water bucket held: the obsidian search's strip at the diamond "
                          "strip's height (y 12), no look around nor climb to y 15, the cast only at lava within 6 (the "
                          "tunnel's), no remembered lava gone back to; off, the search at y 15 and the cast at any lava in "
                          "view or remembered (40 of 63 diamond strips from spawn had lava within 6 of the tunnel, the casts "
                          "at lava 5 to 46 away gave up on the walk; lane/obsid); with it, a cast's pour has two looks "
                          "around on the spot after it (the obsidian under the draining water)"},
    [MO_OB_POOL_MIN] = {"obsidian_pool_min", "off,on,near", 0,   /* lane/obsid2: off the default (the x4 case) */
                        "on: a cast not begun at a pool with fewer than 4 lava sources in view (joined to the target, as "
                        "the eye has seen them), the cell given up and the strip on; off, any pool (lane/obsid's unit "
                        "case: a 1-source pool gives 1 obsidian in 6 of 32 envs, a 4-source pool 4 in 16); near, the "
                        "same count made at the pour, from where the cast stands (from a tunnel's edge 1 to 3 of a "
                        "side cave's 10 sources show; lane/obsid2)"},
    [MO_OB_FACES] = {"obsidian_faces", "off,on", 0,
                     "on: obsidian in the cone counts as seen where the line to one of its open faces (the top, a side "
                     "to air or a liquid) meets it first, not only the line to its centre; off, the centre alone (a "
                     "10-source pool cast from a tunnel's edge: all 10 in the cone, none seen; lane/obsid2)"},
    [MO_OB_GLANCE] = {"obsidian_glance", "off,walk", 0,
                      "walk: the obsidian search's strip (the diamond pickaxe and a water bucket held) walks into each "
                      "cell with the look 45 degrees off the tunnel, left and right in turn, the strafe key holding the "
                      "walk on the tunnel, and looks from there: lava in a side cave beside the tunnel is out of the "
                      "forward look (lane/obsid2)"},
    [MO_OB_WALK] = {"obsidian_walk", "line,around,dig,fill", 0,
                    "around: the obsidian macro's walk to a target (and into a dug pit for its drop) that has not moved "
                    "for 8 steps takes a side step along the free cardinal direction toward it, the look kept on the "
                    "target; line, the straight walk alone (from a tunnel to a side cave's pool it stood at the corner: "
                    "obsidian dug and left in its pit; lane/obsid2). dig (lane/flint): around, and a remembered water "
                    "or lava source more than 3 columns off or not one block under the feet is no job yet: the "
                    "obsidian search's strip tunnels to it (steered at it, its stairs to the target's level plus one, "
                    "the strip's own cell, floor, lava, liquid and gravel tests) and the job begins on arrival; the "
                    "strip cast's gate then lets the remembered lava be taken. The flint_and_steel starts: the fill's "
                    "straight walk to remembered water gave up in 916 of 964 jobs on s42k50 (walls in the line, drops). "
                    "fill: dig's walk for the fill's remembered water alone, the cast's remembered lava as with around "
                    "(the strip cast's gate kept)"},
    [MO_OB_YIELD] = {"obsidian_yield", "off,on", 1,
                     "on: the obsidian search's look sees a target it would take (obsidian, water for an empty bucket, "
                     "lava within 6 for a water bucket): the strip mine gives the step up there and then, so the "
                     "obsidian macro starts on it; off, the strip's own want (iron, diamonds) may go on digging past it "
                     "(lane/obsid2)"},
    [MO_OB_RESUME] = {"obsidian_resume", "here,line", 1,
                      "line: a cast begun from the obsidian search's strip keeps the strip's line (the feet's cell and "
                      "its heading); with nothing left in view after it the strip digs back onto that line and goes on "
                      "along its heading (600 steps at most); here, the strip goes on from where the pool left the "
                      "player (inside the first pool's cave, on a line beside the tunnel, it passed a pool on the "
                      "tunnel's other side; lane/obsid2)"},
    [MO_OB_TRIP_REACH] = {"obsidian_trip_reach", "near,far", 0,
                          "how far the water trip (obsidian_water_trip on) walks to the surface water kept: near, within "
                          "128 across and 8 up or down; far, 336 and 16 (from spawn 12 of 47 kit runs at their first sky "
                          "after the pickaxe had the water kept 129 to 322 across, 4 over 8 up or down; lane/obsid2)"},
    [MO_OB_TRIP_NIGHT] = {"obsidian_trip_night", "off,on", 0,
                          "on: the water trip begins and walks by night too; off, by day alone (5 of 47 kit runs reached "
                          "the sky at night; lane/obsid2)"},
    /* lane/obsid2: the portal's build (mods/macros/portal.c) */
    [MO_PT_BASE] = {"portal_base", "any,ground", 1,
                    "where the frame's place is taken: any, the feet's cell at the build's first step (mid-jump, a "
                    "cell too high: the bottom row's places then find no floor); ground, at the first step on the "
                    "ground"},
    [MO_PT_TOWER] = {"portal_tower", "count,stand,centre,ground", 2,
                     "a tower step's end: count, the cobble count fell and the feet stand one higher (a dug block's "
                     "drop picked up hides the fall); stand, the feet on the ground one higher; centre, stand and the "
                     "feet kept on the column's cell (a tower drifted a cell hides the frame's bottom from its top, "
                     "and one in the frame's cells keeps the portal from forming); ground (lane/bportal), centre's and "
                     "the jump's base height taken only with the feet on the ground (after a fall back centre took it "
                     "from a rising jump, and the landing one block up never matched it)"},
    [MO_PT_ROOM] = {"portal_room", "frame,wide", 1,
                    "the cells dug before the build: frame, the frame's 4x5 and the column over the head; wide, also "
                    "the feet's row beside the feet (r -1, 1 and 2, h 0 to 4: in a 1x2 tunnel the wall beside the "
                    "feet hides the frame's far corner, under a low roof the rock beside the tower's top its far "
                    "upper cells)"},
    [MO_PT_RETURN] = {"portal_return", "off,on", 0,
                      "in the Nether, before the tower is built: after a hit moves the player over 1.2 blocks from "
                      "the remembered portal base, walk back there before retrying the current dig or place. Off, "
                      "a blaze moved s42n4g's first corner two blocks away and it timed out without placing obsidian"},
    [MO_PT_BRICK_PICK] = {"portal_brick_pick", "off,on", 0,
                          "when the Nether portal room digs brick blocks 112, 113 or 114, use the best pickaxe as for "
                          "stone. Off, the generic tool table has no entry for those blocks: s42n4g's step 1 dug "
                          "Nether brick with cobblestone selected until the 260-step timeout"},
    /* lane/food2: the food macro (mods/macros/food.c) */
    [MO_FOOD_EAT] = {"food_eat", "meat,any", FE_MEAT,
                     "what the food macro eats: meat, pork, beef and chicken alone (cooked first); any, also bread, "
                     "apples, fish, carrots, potatoes and the rest (the most filling first), rotten flesh only at "
                     "FOOD_FLESH_AT or under with nothing else held"},
    [MO_FOOD_HUNT] = {"food_hunt", "view,stock,stock32", FH_VIEW,
                      "when the hunt starts: view, an animal in view on every fifth step with under 6 meat held; "
                      "stock, on any step with under FOOD_STOCK held; stock32 (lane/chain2), under 32 held (s42k3's "
                      "End states: 49 of 1,011 held any food, the fight an hour of crystals and sword)"},
    [MO_FOOD_BREAK] = {"food_break", "off,on", 0,
                       "on: eating cuts into a running macro (not a fight, an open screen, the craft macro, the lava "
                       "escape) at FOOD_BREAK_AT food or under with food held; off: it waits for the macro to end"},
    [MO_FOOD_SCOUT] = {"food_scout", "off,look,walk,early", FS_OFF,
                       "the look for food animals by day at the surface, short of meat with a pickaxe, once the food "
                       "level is at FOOD_SCOUT_AT or under: off; look, a turn on the spot; walk, the turn, then a "
                       "square spiral of growing legs with a turn at each leg's end, until an animal is in view (the "
                       "hunt then) or FOOD_SCOUT_MAX steps; early, walk at any food level (the first pickaxe made at "
                       "the surface, before the mine)"},
    [MO_FOOD_CHASE] = {"food_chase", "off,far", FC_OFF,
                       "the hunt's chase: off, an animal out of view walked after 40 steps and the hunt over at 400 "
                       "(the fight's limit); far, walked after to its last place (FOOD_CHASE_LOST steps at most) and "
                       "the hunt's own 1200-step limit"},
    [MO_FOOD_COOK] = {"food_cook", "off,furnace,make,pearl", FK_OFF,
                      "raw meat cooked: off; furnace, the craft macro's smelt takes the largest raw pork, beef or "
                      "chicken stack once FOOD_COOK_N are held and no iron ore waits, at a furnace in reach or held "
                      "(none is made for it); make (lane/chain2), and a furnace made for it from 8 cobblestone when "
                      "none is (s42k3f's snowballs states held 12 to 18 raw pork, coal and 64 cobblestone, no furnace: "
                      "45 to 54 food points against the 160 the End wants); pearl, as make plus a daylight "
                      "cook before the next pearl night when at least eight raw meats and fuel are held"},
    [MO_FOOD_FUEL] = {"food_fuel", "off,wood", FF_OFF,
                      "wood: with raw meat, a furnace and no coal near the stronghold, the surface wood errand "
                      "keeps sixteen planks' worth of logs or planks before the cook; four planks may go to a "
                      "table and eleven fuel planks can cook sixteen meats (vanilla: 300 burn ticks a plank, "
                      "200 ticks an item)"},
    [MO_FOOD_TRIPS] = {"food_trips", "many,two,one,pearl", FT_MANY,
                       "the scouts an episode: many, FOOD_SCOUT_N (4), food held or not; two or one, that many, and "
                       "none while any food is held (one trip's hunts stock the run); pearl, as many until the pearl "
                       "stage, then no trip cap while the pearl day leg yields to a scout, hunt or cook when the "
                       "food bar is under 18 or fewer than 8 cooked meats are held; on a night pillar, no new enderman "
                       "is provoked below full health (s42n3: 53 of 54 pearl_7 nodes held 2 food-bar points or less, "
                       "their steps 0% food)"},
    /* lane/diamond: the strip's wood errand and the bucket portal's turn before the diamond pickaxe (strip2.c) */
    [MO_STRIP_WOOD] = {"strip_wood", "digs,chain,early", SW_CHAIN,
                       "digs: the strip's wood errand (lane/strip3) only while the strip has iron or diamonds to dig; "
                       "chain: also while the iron or diamond pickaxe waits on wood (ingots or diamonds held, no sticks), "
                       "every pickaxe the chain still needs counted in the wood kept (lane/diamond); early: before "
                       "going below the surface, keep ten planks' worth for the later table and diamond pickaxe"},
    [MO_DIA_FIRST] = {"dia_first", "off,on", 1,
                      "on: the bucket portal not begun before a diamond pickaxe is held while the strip has diamonds to "
                      "dig (an iron pickaxe held); off, it begins at flint and steel and two buckets (lane/diamond)"},
    /* lane/pearlhunt's climb_way (unmerged there), here for both of lane/strip3's climbs */
    [MO_CLIMB_WAY] = {"climb_way", "stair,pillar", CW_PILLAR,
                      "the strip's climbs (out of the depth, the wood errand): stair, a stair up, three blocks dug a "
                      "level; pillar, with the feet under y 52 and 8 cobblestone held, the block over the head dug and "
                      "cobblestone placed under a jump, one block dug a level (sand, gravel, a liquid over the head, two "
                      "short jumps or a slow level: the stair for a while). A stone pickaxe lasts 131 blocks: the "
                      "stair's 3 a level and its turns spent it before the wood errand reached the surface (lane/diamond)"},
    [MO_CLIMB_SWIM] = {"climb_swim", "off,on", 1,
                       "on: the eyes under water within 40 steps of a strip climb's step (the climb dug up into a sea's "
                       "or a lake's floor), the surface macro's swim up begins at once; off, it waits for the air under "
                       "120 as anywhere else (lane/diamond)"},
    [MO_STRIP_WOOD_NIGHT] = {"strip_wood_night", "go,wait", SWN_WAIT,
                             "strip_wood chain's wood errand at night (the day's time 12500 to 23500, the sky's): go, as "
                             "by day; wait, not begun nor kept on while a pickaxe is held, the strip digging on below "
                             "(lane/diamond)"},
    [MO_NT_SAFE_DIG] = {"nether_safe_dig", "off,on,deep", 2,
                        "on: the Nether walker never digs a block the player's own box stands on (a stair down from a "
                        "deck's edge dug the floor away: 118 of 128 frozen runs from the library's Nether start fell "
                        "50 blocks into the lava sea, 0 with it; the random-policy case from gold-g7 lost every rod "
                        "with it, 21 and 24 of 32 to 0 at sampler seeds 1 and 2); deep (the default): only such a "
                        "block with no floor in the 4 cells under it (open, lava or not loaded): the frozen runs' "
                        "falls 74 of 128 to 0 and the random case's rods kept, 21 and 24 of 32; lane/rod)"},
    [MO_NT_SPREAD] = {"nether_spread", "off,on", 0,
                      "the Nether search's side and bearing (at the arrival and each leg's turn): off, the policy's "
                      "camera bin that step; on, a draw of the env, the episode and the step (-4 to 4 bins of 30 "
                      "degrees: the frozen policy turned one bin in every run from a start; the harvested starts' "
                      "frozen runs 26 distinct trajectories to 47; off by default: the random-policy case from gold-g7 "
                      "with it, 21 and 24 rods of 32 to 16 and 16; lane/rod)"},
    [MO_NT_SEARCH_Y] = {"nether_search_y", "32,40,48", 0,
                        "the Nether search's feet height off the fortress (the lava sea's top is 31: a tunnel at 32 "
                        "beside it flooded, 46 of 94 deaths from the harvested starts at one cell; 40 and 48 died in "
                        "lava at the portal in 32 of 32 idle envs; lane/rod)"},
    [MO_NT_SEAL_AIM] = {"nether_seal_aim", "off,on", 1,
                        "the Nether macro's seal of a lava cell: off, the crosshair on the cell's centre (it passes "
                        "through the lava to the block behind, whose face does not border the cell: no seal placed); "
                        "on (the default), on a face of a solid block bordering the cell that the eye sees (the top of "
                        "the one under it first; a harvested start's idle deaths 28 of 32 to 21; lane/rod)"},
    [MO_NT_FEET_FILL] = {"nether_feet_fill", "off,on", 0,
                         "in the Nether, standing on a block under one edge of the box with the cell under its middle "
                         "open or lava (a portal's frame over the lava sea), that cell filled first against the "
                         "supporting block's face (off: every bridge aimed at the open cell's side and none placed: 32 "
                         "of 32 envs of a harvested start stood at the portal; on placed it, but the bridge still had no "
                         "fill in the hotbar, and two other starts' idle deaths rose, 21 to 26 and 19 to 25; lane/rod)"},
    [MO_NT_HOOD_FULL] = {"nether_hood_full", "off,on", 0,
                         "the hood closed at the feet too (four sides there besides the roof and the four at the "
                         "head: a skeleton shot the healing player through the open feet level; lane/rod)"},
    [MO_NT_ARRIVE_FILL] = {"nether_arrive_fill", "off,on", 1,
                           "the fill blocks fetched into the hotbar in the portal too, its frame under the box, where "
                           "nether_footing finds no footing (a portal over the lava sea: no bridge was ever placed; "
                           "lane/rod)"},
    [MO_NT_HOOD_HIT] = {"nether_hood_hit", "off,on", 0,
                        "hurt while healing in the hood (it does not cover the player): the hood given up 400 steps, "
                        "the fight's rules next (a skeleton shot the healing player through it; lane/rod)"},
    [MO_NT_ESC_FIRST] = {"nether_esc_first", "off,on", 0,
                         "in the Nether the lava escape walks out before any seal while the player's box is in lava "
                         "(off: the seal first, the player sealing round itself in the lava sea; lane/rod)"},
    [MO_NT_OFF_HOLD] = {"nether_off_hold", "off,on", 1,
                        "in the Nether the macro's off steps (no way on found) held still and crouched with a slow "
                        "look round, never handed to the policy (off: the policy's turn; lane/rod)"},
    [MO_NT_LAVA_BACK] = {"nether_lava_back", "off,on", 0,
                         "in the Nether the lava escape walks back along the way's own cells (the feet's crumbs), a "
                         "jump only up a block (off: the strip's escape, forward and jump at its last safe cell; "
                         "lane/rod)"},
    [MO_NT_PORTAL_EXIT] = {"nether_portal_exit", "off,on", 1,
                           "in the Nether, standing in the portal without a floor under the whole box (its frame's "
                           "edge over the lava sea), the player walks through the portal to the nearest cell with a "
                           "floor first (off: the walker judged the portal's cells bad and stood there, and the client "
                           "closes every screen in a portal, so the fill never left the main inventory: 32 of 32 envs "
                           "of a harvested start stood at the portal all episode; lane/rod)"},
    [MO_NT_ENTRY_FILL] = {"nether_entry_fill", "off,on", 1,
                          "with the nether macro on, the portal builders' walk into a lit portal (portal.c, bportal.c) "
                          "first moves fill blocks (cobblestone, netherrack) from the main inventory into the hotbar "
                          "when it holds under 16 (the arrival's bridges need them, and no screen stays open in a "
                          "portal; the harvested starts came through with none there; lane/rod)"},
    [MO_NT_DROPS] = {"nether_drops", "off,on", 0,
                     "the Nether macro's walk to a blaze's rod: on, each kill the stats screen credits (the only kind "
                     "that drops a rod) leaves its place in view as a drop to walk to, nearest first, before a new "
                     "fight within 6 blocks; off (the default), one walk to wherever the fight's lock was lost (killed "
                     "or flown off), dropped by the next fight. gold-g8 without its rods, 12,000 steps, random policy "
                     "seeds 1 and 2: on 207 and 213 credited kills against 293 and 274, rods held 89 and 92 against "
                     "109 and 122, 7 rods 0 and 0 against 4 and 4: the walks cost fights; lane/rod)"},
    [MO_NT_BLAZE_SEEK] = {"nether_blaze_seek", "off,on,deck", 0,
                          "searching the fortress with no spawner known, a blaze in view past the fight's reach walked "
                          "toward at the fortress's height (blazes keep near their spawner) instead of the shy's "
                          "back-off; deck, the same walk only onto the fortress's own floor (brick under every step: "
                          "no bridge, no drop, no lava floor; on walked off over the lava sea; lane/rod)"},
    [MO_NT_SEARCH] = {"nether_search", "tunnel,surface", 0,
                      "the Nether search off the fortress: tunnel, the level walk at nether_search_y digging through "
                      "what is in the way (a dug cell beside the lava sea lets it in: most of the harvested starts' "
                      "deaths); surface, a player's walk on the netherrack as it lies: nothing dug, a natural step up "
                      "or a drop of 3 at most, a gap bridged up to 6 cells in a row (a wider one turned from), and a "
                      "3-block pillar and a look round every 600 steps (lane/rod)"},
    [MO_NT_HOOD_HP] = {"nether_hood_hp", "8,14", 0,
                       "the health at or under which the Nether macro walls itself in (the hood) at the blazes: 8, "
                       "as before; 14 (a blaze's melee is 6: three in melee took a harvested start's player 16 to 10 "
                       "to 4 to 0 in 65 steps, the hood begun at 4; lane/rod)"},
    [MO_NT_SWEEP] = {"nether_sweep","one,back,draw,minus", 0,
                     "the Nether search's z legs: one, every one along the arrival's facing's sign (a side step "
                     "between: the harvested starts faced +z and walked to z 200 while the fortress lay at -z); "
                     "back, each the other way and 48 blocks longer than the last (48, 96, 144 ...; 32-block side "
                     "steps), past the arrival's z and as far the other side; draw, one way along z drawn per env and "
                     "episode, a player's coin; minus, every z leg toward -z: NOT a player's choice, an eval-only "
                     "upper bound for seed 42's harvested portals, whose fortress lies at -z (the heading a policy "
                     "that chose right would take; lane/rod)"},
    [MO_NT_ORACLE] = {"nether_oracle", "off,fortress,spawner,home,tunnel", 0,
                      "PRIVILEGED (lane/rod; for lane/chain's search, never in the benchmark's numbers: it reads the "
                      "server's structure data and blocks, nothing a player sees): off; fortress, the search's z and x "
                      "legs' signs toward the nearest throne piece of the Nether fortress map (netherview_oracle); "
                      "spawner, also the throne's spawner block from the server's blocks, given once on the fortress, "
                      "and the walk to the cell 3 under it along a shortest path over the server's cells, digging and "
                      "bridging (no lava or fire beside; netherview_oracle_path), the block over the head dug there; "
                      "home, also with 7 rods the walk to the arrival's portal cell, into it, and a step 2 cells out "
                      "and back after 120 steps in it with no travel (vanilla's portal cooldown); tunnel, also a dug "
                      "approach to the standing cell over the mapped top brick before the fortress is entered (seed "
                      "1's ledge at y 89 over brick at y 69), then a closed departure from the spawner (the hole over "
                      "the head sealed, a wait until not burning and no "
                      "blaze in view, a dig down through the foundation) and that walk as a dug 1x2 tunnel: the "
                      "tunnel planner (NV_PATH_TUNNEL: solid dug cheap, open cells dear, the lava sea's level avoided) "
                      "names each next cell, the digger (nt_digger) digs and walks it, refusing lava beside or over "
                      "its cells (the raw blocks); a refused cell falls back to the digger's own rules (toward the "
                      "portal, travel at y 45 or over)"},
    [MO_NT_START] = {"nether_start", "off,locate,throne", 0,
                     "PRIVILEGED: before any fortress piece is loaded nearby, locate the nearest candidate start "
                     "from the vanilla seed test (locate), or generate nearby structure boxes and choose the closest "
                     "blaze-throne centre (throne). A sealed tunnel drives toward the target and the search keeps "
                     "each eight blocks of distance closed inside the Nether rung; off, the old sweep"},
    [MO_NT_ENTRY] = {"nether_entry", "off,sealed", 0,
                     "PRIVILEGED: choose the nearest enclosed corridor or crossing from generated fortress boxes; "
                     "stair in sealed 32-block tunnel stages to its brick floor's standing height, then enter through "
                     "a wall, before climbing in the open. Off, the ordinary fortress approach"},
    [MO_NT_SP_TUN] = {"nether_spawner_tunnel", "off,on", 0,
                      "on, once the fortress's spawner is known, use the sealed dig-and-bridge path to the cell under "
                      "its room instead of the exposed path; off, the existing walk and bridge"},
    [MO_NT_SP_BREAK] = {"nether_spawner_break", "off,on", 0,
                        "after seven blaze rods, approach the seen spawner under cover and mine its cage before "
                        "starting the home portal; fall back to the existing portal route after a bounded attempt"},
    [MO_NT_CHAMBER] = {"nether_chamber", "off,on,stronghold", 0,
                       "after seven rods, search for an enclosed netherrack cell near the seen spawner and tunnel "
                       "directly to it, even when the arrival portal was not remembered; build the return portal "
                       "under its rock roof at least eight blocks from the cage. With no direct path, "
                       "nether_oracle=tunnel may still retreat. stronghold instead tunnels in short legs toward the "
                       "located portal room's Nether coordinates before choosing a roofed cell for the home frame. "
                       "Its final portal build currently needs a long uncut segment. Use portal_room=wide to clear it"},
    [MO_NT_EXIT_COVER] = {"nether_exit_cover", "off,on", 0,
                          "at seven rods stop fighting and collecting drops, dig two safe floor blocks with a pickaxe "
                          "to descend below the spawner and place a block overhead, then heal under cover before "
                          "the chamber or home route; no new blaze lock starts after the seventh rod"},
    [MO_NT_TRIM] = {"nether_trim", "off,on", 0,
                    "on, before the overworld portal is built and during the sealed Nether approach, keep three "
                    "inventory slots free by throwing junk or cobblestone beyond 128; in the Nether, also trim "
                    "netherrack beyond one stack before entering the fortress or under a spawner on the sealed route. "
                    "Off, no portal-route pack trim"},
    [MO_NT_ORACLE_LOCK] = {"nether_oracle_lock", "off,on", 0,
                           "PRIVILEGED (lane/rod; never in the benchmark's numbers): at the blaze spawner the fight's "
                           "target is the nearest blaze in the client's entity list within 16 (through walls, out of "
                           "view: a lock never lost), looked at, closed on crouched within 5, struck whenever the "
                           "crosshair is on a blaze, and a rod's drop within 10 (the server's items) walked to along "
                           "the path when no blaze is within reach; off, fight()'s lock on a blaze in view"},
    /* lane/pearlgo (lane/pearlhunt's, unmerged there) */
    [MO_CRAFT_RETURN] = {"craft_return", "off,on", 0,
                         "the craft macro's leftovers: off, as before (a one-slot part goes into the grid as its whole "
                         "stack, and what the output's shift-click leaves is dropped on the ground as the screen closes: "
                         "a pearl and 12 blaze powder make one eye and drop 11 powder, which ends the pearl hunt); on, "
                         "each such grid slot is shift-clicked back after the output"},
    [MO_NETHER_RODS] = {"nether_rods", "7,6", 0,
                        "the blaze rods the Nether macro takes home: 7 (lane/nether's: 14 powder); 6, the 12 eyes' "
                        "powder exactly (the library's Nether start gold-g8 holds 6 and hunted on for the 7th)"},
    [MO_PEARL_NIGHT_FIRST] = {"pearl_night_first", "off,on,day", 1,
                              "the strip mine while the pearl hunt is wanted (night, the overworld, blaze held, pearls "
                              "and eyes short): off, as before (a strip runs on through the night: the blaze rod link's "
                              "night steps were the strip's); on, the strip not wanted, so its climb takes the player out "
                              "of the depth and the hunt begins (and a hunt is not begun under the climb's height); "
                              "day, the same from the pearl step on at any hour (blaze held, nothing left to dig: the "
                              "player out on the land by dusk, not climbing through the night)"},
    [MO_PEARL_WALK] = {"pearl_walk", "sweep,sprint,straight", 0,
                       "the pearl hunt's walk between pillars: sweep, as before (turned toward the longest view every "
                       "60 steps, the policy's camera steering); sprint, the same sprinting; straight, sprinting with no "
                       "turn to the view and no steer (water, skeletons and a stuck walk still turn it; lane/pearlhunt: "
                       "1,359 endermen seen and 202 killed against sweep's 172 and 12)"},
    [MO_NETHER_DONE] = {"nether_done", "dims,held", 1,
                        "when the Nether counts as done for the obsidian search and the bucket portal: dims, as before "
                        "(a visit this episode); held, also while a blaze rod, blaze powder or an eye of ender is held "
                        "(from the library's blaze rod starts the obsidian search dug the day away and the climb back "
                        "took the night)"},
    /* lane/pearlsee */
    [MO_PEARL_LOOK] = {"pearl_look", "walk,turn", 0,
                       "the pearl hunt's look while it walks: walk, as before (pitch 25 down, a 35-degree sweep); turn, "
                       "also every PEARL_TURN_EVERY walk steps the walk stops for a level full turn (PEARL_TURN_STEPS "
                       "steps), the far ground in the frame"},
    [MO_PEARL_TOWER] = {"pearl_tower", "off,on", 1,
                        "the scan's pillar: off, as before (PEARL_TALL blocks, built only where the frame's level rays "
                        "were long, PEARL_OPEN); on, PEARL_TOWER blocks on the first dry cell of the night with no "
                        "long-view gate, a full turn scanned from the top for PEARL_TOWER_SCAN steps (no short-view cut). On is "
                        "the default (lane/pearlsee: with pearl_dip turn, endermen in view 0 to 337 hunt steps)"},
    [MO_PEARL_DUSK] = {"pearl_dusk", "off,on", 0,
                       "pearl_night_first's climb: off, at dusk (the hunt wanted); on, also from PEARL_DUSK_LEAD ticks "
                       "before it (the pearl step held, the overworld): the strip not wanted then, its climb out begun in "
                       "the day's last part so the night is the hunt's"},
    [MO_PEARL_SEE] = {"pearl_see", "cone,frame,pixel", 2,
                      "what the hunt counts as an enderman in view: cone, as before (rlbind.h neth_rl_far: within 75 "
                      "degrees of the look, 64 blocks, the line clear); frame, only inside the 64x64 frame (35 degrees "
                      "either side of the look in yaw and in pitch) and within PEARL_SEE_R blocks (an enderman there is "
                      "4 pixels tall at 64x64; lane/pearlsee: the cone saw past the picture); pixel, inside the frame "
                      "with the box at least PEARL_SEE_PX of its PEARL_SEE_ROWS rows tall (an enderman: 2 rows out to "
                      "about 66 blocks along the look, neth_rl_far's 64 the bound). Pixel is the default (lane/pearlsee's "
                      "second pass, with pearl_sky and pearl_lure: in-view hunt steps 546 to 1,307 and 0 to 368 at eval "
                      "seeds 1000, 1001)"},
    [MO_PEARL_DIP] = {"pearl_dip", "hand,turn", 1,
                      "pearl_night_first's hunt walking into a dip under the land's height (PEARL_SURF_Y): hand, as "
                      "before (given back to the strip's climb, which latches until its stair stands at the height); "
                      "turn, a dip no deeper than PEARL_DIP_Y walked back out of (the heading turned round), and once "
                      "the hunt has stood on the land a hunt begun again anywhere over PEARL_DIP_Y. Turn is the default "
                      "(lane/pearlsee: hunt steps 131,870 to 380,033 and 133,475 to 404,012 at eval seeds 1000, 1001)"},
    /* lane/pearlsee (2nd pass) */
    [MO_PEARL_SKY] = {"pearl_sky", "off,on", 1,
                      "pearl_tower's cell: off, as before (the first dry cell, a roof found by two short jumps); on, "
                      "the look straight up first: a ground roof within PEARL_SKY_DIG (not leaves, a log, sand, gravel "
                      "or a liquid) is dug up through (the block over the head mined before each jump, PEARL_TOWER "
                      "blocks counted from where the sky first shows); another roof within NETH_RL_DEPTH_R (a tree's) is "
                      "walked on from (PEARL_SKY_WALK steps). On is the default (lane/pearlsee's second pass: the "
                      "climb out handed the hunt the step under the land, 817 of 832 climbs ended at a roof; towers "
                      "topped 6 to 89 and 2 to 88 at eval seeds 1000, 1001)"},
    [MO_PEARL_FETCH] = {"pearl_fetch", "cap,stay", 0,
                        "a kill's drop: cap, as before (walked to for PEARL_DROP steps, stood on PEARL_DROP_WAIT); "
                        "stay, walked to for PEARL_FETCH_STEPS steps (round what stops the walk) and stood on until the "
                        "pearl is picked up or PEARL_FETCH_WAIT steps pass"},
    [MO_PEARL_LURE] = {"pearl_lure", "off,on", 1,
                       "a provoked enderman lost from view while the player stands on a tall tower: off, as before "
                       "(its last place watched, then looked for round at 20 down; a far one gone down for); on, the "
                       "tower dug down to PEARL_PILLAR blocks (where a hit meets its head at the foot), the look round "
                       "at PEARL_LURE_PITCH down, never down for a far one (let go after PEARL_LURE_FAR) and the tower "
                       "kept PEARL_LURE_HOLD steps after one last screamed in view. On is the default (lane/pearlsee's "
                       "kill case: an enderman 12 blocks off, 0 kills to 26 and 24 in 32 envs, pearls 0 to 15 and 17)"},
    [MO_PEARL_HIGH] = {"pearl_high", "off,on", 0,
                       "where pearl_tower's tower stands: off, as before (where the walk is when it is time); on, a "
                       "level turn first, then the walk toward the highest ground face seen (the frame's hill row, "
                       "NETH_RL_HILL_PITCH up) while one is PEARL_HIGH_MIN over the feet, at most PEARL_HIGH_WALK steps"},
    /* lane/pearlsee (3rd pass) */
    [MO_PEARL_RESTOCK] = {"pearl_restock", "off,on", 1,
                          "the tower's blocks: off, as before (the hotbar's cobblestone only: golden-g9 holds 62 there "
                          "and 65 in the pack, and its climbs ended out of blocks 30 and 54 times at eval seeds 1000, "
                          "1001); on, under PEARL_RESTOCK_LOW in the hotbar with a stack in the pack, the inventory "
                          "opened on the ground and the pack's largest stack swapped into the hotbar (one click). On is "
                          "the default (lane/pearlsee's third pass: golden-g9's tower runs 5 to 30 of 47 at seed 1000)"},
    [MO_PEARL_HEIGHT] = {"pearl_height", "10,20", 0,
                         "pearl_tower's tower: 10 blocks (PEARL_TOWER) or 20 (PEARL_TOWER_HIGH) over its ground"},
    [MO_PEARL_HOLD] = {"pearl_hold", "3000,1000", 0,
                       "steps a tower's top is held with nothing in view before it is left for the walk and another "
                       "tower: 3000 (PEARL_TOWER_SCAN, as before) or 1000 (PEARL_TOWER_SCAN_SHORT: new ground more often)"},
    [MO_PEARL_ORACLE] = {"pearl_oracle", "off,on", 0,
                         "PRIVILEGED (not a player's information; for the checkpointed search, Elliot 2026-11-10 1:20 am): "
                         "on, the night's walk goes straight to the nearest enderman under the sky in the server's entity "
                         "list (the census, rlbind.h neth_rl_census), no tall tower or long-view pillar; once it is in the "
                         "macro's own view the short pillar's stare and fight (pearl_lure) and the drop's pickup as before"},
    /* lane/chain: the privileged hints, off by default and never in a benchmark's numbers */
    [MO_OB_ORACLE] = {"obsidian_oracle", "off,on", 0,
                      "PRIVILEGED (the search's, lane/chain): on, while the obsidian macro is wanted, every "
                      "OB_ORACLE_EVERY steps the client world's water and lava sources within OB_ORACLE_R blocks "
                      "(what the server sent, seen or not) are put in its memory as if seen, nearest first, so the "
                      "macro walks to them instead of searching; off, only what the eye saw"},
    [MO_BP_ORACLE] = {"bportal_oracle", "off,on", 0,
                      "PRIVILEGED (the search's, lane/chain): on, every OB_ORACLE_EVERY steps of the bucket portal's "
                      "memory look the client world's water and lava sources within OB_ORACLE_R blocks are put in its "
                      "memory as if seen; off, only what the eye saw"},
    [MO_OB_DIG_SKIP] = {"obsidian_dig_skip", "off,on,bad", 0,
                        "on, the obsidian macro's look passes over obsidian it may not dig (lava beside or under it: "
                        "a cast lake's skin over its lava) to the next in view, a rim block over stone; off, the "
                        "nearest obsidian in view is the target and the macro waits on it (lane/chain: 137 of a "
                        "trace's looks held one skin cell over lava); bad (lane/bportal), on's and the targets given "
                        "up passed over too for their 1200 steps, as off has them (on skips that test: a cell given "
                        "up with a block in the line was picked again every 101 steps)"},
    [MO_FOOD_ORACLE] = {"food_oracle", "off,on", 0,
                        "PRIVILEGED (the search's, lane/chain): on, the provisions before the Nether: with 10 obsidian, "
                        "the flint and steel and a diamond pickaxe held and under FOOD_PROV hunger points of food, the "
                        "portal waits, the strip's climb goes to the surface and the scout walks to the server's "
                        "nearest pig, cow or chicken (the census, rlbind.h; the path by walk_to within its range), the "
                        "hunt taking over in view, until FOOD_PROV are held; off, the scout's spiral and its rules"},
    [MO_PEARL_REACHABLE] = {"pearl_reachable", "off,on", 0,
                            "PRIVILEGED, with pearl_oracle on only: off, its target is the nearest enderman; on, the "
                            "nearest the engine's pathfinder has a path to (one within 40 blocks with no path is "
                            "skipped for 400 steps)"},
    [MO_OB_CARRY] = {"obsidian_carry", "off,on,frame,fallback", 0,
                     "on, the obsidian macro mines until OB_CARRY_N (20) are held, and the portal macro's overworld "
                     "frame waits for them (it spends 10, the rest carried; the bucket portal casts its own and "
                     "spends none): the Nether's way home is carried for nether_portal_home; off, it stops at 10; "
                     "frame (lane/bportal), off's 10, and on the first arrival in the Nether, before the Nether macro "
                     "goes on, 10 mined from the arrival portal's own frame (out of the portal, the cells read from "
                     "the client world, within 3000 steps of the arrival); fallback (lane/bportal, the orchestrator's "
                     "3:45 am revision), on's 20 until obsidian_fallback_gap steps pass with 10 or more held and none "
                     "gained (latched for the episode), then the first frame at 10, and in the Nether frame's mining "
                     "of only what is short of 10 (none with 10 or more held)"},
    [MO_NT_PORTAL_HOME] = {"nether_portal_home", "off,on", 0,
                           "on, in the Nether with seven rods' worth (rods + (powder + eyes + 1) / 2), 10 obsidian, "
                           "7 cobblestone and the flint and steel held, the Nether macro yields and the portal macro "
                           "(macro portal) builds, lights and enters a frame where the player stands: no walk home; "
                           "off, the Nether macro's own way home"},
    [MO_DIA_ORACLE] = {"diamond_oracle", "off,on", 0,
                       "PRIVILEGED (the search's, lane/chain): on, while the strip digs for diamonds (an iron pickaxe, "
                       "no diamond pickaxe, under 3 diamonds), every DO_EVERY steps the client world's nearest diamond "
                       "ore within DO_R blocks across and under y DO_TOP (seen or not, no lava beside it) is the "
                       "strip's target: its tunnel's level the ore's and its direction the axis with more to go, the "
                       "strip's own lava, liquid and floor rules kept (a turn of theirs holds the steering DO_HOLD "
                       "steps; a target with DO_TURNS such turns or DO_GIVEUP steps is given up); off, the strip's "
                       "own direction and level 12"},
    /* lane/shold */
    [MO_SH_ORACLE] = {"stronghold_oracle", "off,locate", 0,
                      "PRIVILEGED (lane/shold; for lane/chain's search, never in the benchmark's numbers: it reads the "
                      "world's structure data and the server's blocks, nothing a player sees): off, the eyes thrown "
                      "and followed; locate, no eye thrown: the portal room from the seed's stronghold pieces "
                      "(shoracle.h), the walk there over the server's cells (walk, swim and dug moves, the hole for "
                      "the night as stronghold_night), a dug staircase down into the room (no shaft, nothing liquid "
                      "beside, nothing that falls over the head), the silverfish spawner broken, an eye put in each "
                      "frame the server's blocks show open, onto the portal"},
    [MO_DG_ORACLE] = {"dragon_oracle", "off,crystals", 0,
                      "PRIVILEGED (lane/shold; for lane/chain's search, never in the benchmark's numbers: it reads every "
                      "crystal copy the client holds, seen or not, and the server's End blocks): off, the crystals "
                      "shot as seen; crystals, each crystal in turn (nearest first) taken with the sword from a "
                      "tower beside its pillar whose top is one under the pillar's bedrock cap (the blast's every "
                      "ray to the body crosses the cap: no harm), the tower dug down after; no bow needed"},
    [MO_DG_CRYSTALS] = {"dragon_crystals", "bow,snowball", 0,
                        "lane/shold: what the dragon macro shoots the crystals with: bow, arrows from the bow (and at "
                        "the dragon); snowball, thrown snowballs on the same aims and lobs with EntityThrowable's "
                        "flight (1.5 blocks a tick, gravity 0.03), never at the dragon (any projectile ends a crystal: "
                        "EntityEnderCrystal.attackEntityFrom whatever the amount); no bow needed"},
    /* lane/rod */
    [MO_ARMOR_KIT] = {"armor_kit", "off,iron,full", 0,
                      "PRIVILEGED (the ore found in the client world's blocks unseen, the way to it planned over the "
                      "server's; for lane/chain's search, never in the benchmark's numbers; mods/macros/armor.c): off; "
                      "iron, in the overworld with a stone or better pickaxe, the iron for boots, leggings and a "
                      "chestplate (19 ingots, a helmet's 5 more when the ore allows, and what the iron plans spend on "
                      "a bucket and a flint and steel) dug from the nearest iron ore with no lava or water round it "
                      "(a 1x2 tunnel to it, dig.c), smelted by the craft macro, the pieces crafted on a table (plans "
                      "C_IBOOTS, C_ILEGS, C_ICHEST, C_IHELM) and put on by shift-clicks in the inventory screen; "
                      "full also mines the helmet's five ingots and, with sword_kit=iron, two for an iron sword"},
    [MO_SNOW_KIT] = {"snow_kit", "off,on,far", 0,
                     "PRIVILEGED (the snow found in the client world's blocks unseen, the way to it planned over the "
                     "server's; for lane/chain's search, never in the benchmark's numbers; mods/macros/snow.c): off; "
                     "on, in the overworld under 64 snowballs: a wooden shovel made on a table when no shovel is held "
                     "(one plank, two sticks: plan C_WSHOVEL), then the nearest snow layer or snow block within 48 "
                     "across walked to (dig.c's tunnel) and dug with the shovel (a layer 1 snowball, a block 4), until "
                     "64 are held; the shovel kept. far (lane/snowleg), as on, and when no snow is within 48: the "
                     "trip to the nearest snowy biome (the world generator's biomes, shoracle_snow_column: ice plains, "
                     "ice mountains, cold beach, cold taiga and its hills) in legs of SN_LEG blocks along lane/shold's "
                     "walk over the server's cells (walk, swim and dug moves), stood on land from SN_DUSK ticks before "
                     "night to the morning, the step given back to eat; with no shovel and too little wood for one, "
                     "the nearest trunk's foot log dug (shoracle_log) until there is, and the table made from the "
                     "planks when none is held or near"},
    /* lane/snowleg */
    [MO_SNOW_FIRST] = {"snow_first", "off,on", 0,
                       "PRIVILEGED with snow_kit (lane/snowleg; for lane/chain's search): on, with snow_kit not off and "
                       "under 64 snowballs held in the overworld, the stronghold macro does not latch at its twelve "
                       "eyes and the snow macro starts in its place (start rank 10, before the stronghold's), until 64 "
                       "are held, the snow macro gives up (snow_kit on with no snow in its scan; far with no snowy "
                       "column within SN_FAR_R; SN_FIRST_MAX steps since the wait began) or the player dies; then the "
                       "stronghold macro latches as before (its oracle's walk back by itself). Off, as before"},
    /* lane/pearlhome */
    [MO_PEARL_HOME] = {"pearl_home", "off,on", 0,
                       "PRIVILEGED (lane/pearlhome; for lane/chain's search: the world's structure data and the "
                       "server's blocks): on, from the pearl step on (blaze held, the overworld, pearls and eyes under "
                       "12) the pearl macro owns the days too: by day a travel leg toward the stronghold's portal room "
                       "(shoracle_locate, lane/shold's walk over the server's cells: walk, swim and dug moves) until "
                       "PEARL_DUSK_LEAD ticks before night or PH_NEAR blocks from the room, then on to land (feet on "
                       "the ground, no water within PH_SHORE blocks) and stood there; at night on land the hunt "
                       "(nothing outranking it but eating; the y-62 gate of pearl_night_first not applied), in water "
                       "the leg on to land first; the food macro's hunt, scout and eating come first by day. Off, as "
                       "before"},
    [MO_PEARL_TARGET] = {"pearl_target", "sky,any", 0,
                         "PRIVILEGED, with pearl_oracle on only: sky, as before (the nearest enderman under the sky, or "
                         "provoked); any, then also the nearest of any enderman the census lists (the server's world "
                         "within 128 blocks: in a cave, under trees), walked to on the pathfinder's way"},
    [MO_PEARL_TRIM] = {"pearl_trim", "off,on", 0,
                       "lane/chain: off; on, at the pearl stage by day or night, on the ground and off a pillar, with under "
                       "strip2's ST_FREE_MIN free slots the pack's surplus (cobblestone past ST_KEEP_COBBLE, dirt, netherrack, "
                       "gravel, saplings) thrown from the inventory screen by strip2's errand (e43i, easy: 36 of 36 "
                       "stacks full at pearl_8, a night's kills and no ninth pearl)"},
    [MO_FOOD_SWIM] = {"food_swim", "off,on", 0,
                      "lane/pearlhome (2nd pass): on, the food hunt (the fight's lock with e->hunt) with the eye under "
                      "water holds the jump (swum at the top, as the scout does), and under FIGHT_SWIM_AIR air looks "
                      "up and swims for it before chasing on (lane/chain's macro set runs no surface macro, and its "
                      "pearl_8 day's drowning was a hunt 538 steps in water, looking down at the animal). Off, as "
                      "before"},
    [MO_SNOW_HEAL] = {"snow_heal", "off,on", 0,
                      "lane/chain: off; on, the snowballs' trip (snow_kit's, after the twelve pearls) not begun under "
                      "SN_HEAL_HP health while the player heals: the food bar at 18 or over (regeneration), or food "
                      "held for the food macro to eat (e43q's eleventh pearl at health 5.4 before a 400-block trip, "
                      "where lane/snowleg's losses were deaths)"},
    [MO_SWORD_KIT] = {"sword_kit", "off,on,iron", 0,
                      "lane/chain: off; on, in the overworld with no sword held and a stick, a table held or near: the "
                      "craft macro plans an iron sword (2 ingots held) or a stone one (2 cobblestone) before its other "
                      "plans (the dragon macro starts only with a sword in the hotbar, or a bow and arrows; e43z's End "
                      "segments: 191, the macro off in every one, 180 deaths near the void). iron also upgrades a stone "
                      "sword when 2 ingots are held; the pearl hunt gives the craft step back on land for that plan "
                      "(s42n3: 2,051 pearl nodes held a stone sword; 2,022 held 2+ iron ingots, 2,049 a table)"},
    [MO_SWORD_STOCK] = {"sword_stock", "off,two,one", 0,
                        "lane/chain2: with twelve eyes before the stronghold, craft fresh reserve swords; two keeps "
                        "the original pair, one needs a single reserve. Iron first when two ingots are spare after "
                        "the armor plan, else stone from cobblestone and sticks; one also makes a table from held "
                        "wood when that is the missing step. Hold the stronghold latch until the reserve is carried; "
                        "the dragon fetches it when the hotbar sword breaks"},
    [MO_PEARL_ARMOR] = {"pearl_armor", "off,on", 0,
                        "lane/chain2: off; on, with armor_kit and pearl_home, on land in the overworld while armor_kit "
                        "still wants a piece: the pearl macro's travel leg or walk (not a climb, a pillar, a hide) gives "
                        "the step back, so armor_kit (which starts last) begins (s42k2: armor_kit drove 0% of 6,095 "
                        "on_land states' steps, the leg 62%, with 16 iron ore, coal and a furnace held)"},
    [MO_ARMOR_FUEL] = {"armor_fuel", "off,coal", 0,
                       "lane/chain2: off; coal, with armor_kit, iron ore held that the fuel held cannot smelt (a coal "
                       "smelts 8, two planks 3) while a piece is still wanted: the nearest coal ore dug as the iron is "
                       "(s42k3's on_land states held 18 ore, a furnace, one plank and no coal: 9,504 of 9,833 never "
                       "smelted)"},
    [MO_SH_FOOD] = {"stronghold_food", "off,wait,surface,wait80", 0,
                    "lane/chain2: off; wait, with twelve eyes in the overworld the stronghold macro's latch held back "
                    "while the food held is under SH_FOOD_FLOOR (160) points, at most SH_FOOD_MAX steps, the strip and "
                    "the seek standing down as for the stronghold: the food macro's hunt and the cook run (s42k3f: 4,900 "
                    "of 4,921 states from the sword on held 0 to 7 cooked meats; the steps went to the dragon, the "
                    "stronghold and the strip); surface, as wait but the strip's climb alone takes a player below "
                    "the food scout's y 62 threshold up to that height; the policy's raw mine lock yields to this "
                    "food trip while the wait is active; wait80 is wait with an 80-point floor (the proved seed-1 "
                    "dragon chain consumed 48 points from stronghold start to dragon death)"},
    [MO_FOOD_PROVISION] = {"food_provision", "off,on,armor_first", 0,
                           "on, after the Nether take the player to open surface before the armor and pearl steps; "
                           "once twelve pearls or eyes, a sword and full iron armor are held, only the surface climb, "
                           "food scout and hunt, cook and night shelter act until 160 held food points; after two "
                           "scouts find no animal, move one dry leg and try again. Armor_first gives armor the turn "
                           "after the Nether until all fifteen points are worn, then uses on's food stage. Off leaves "
                           "every macro's order as it was"},
    [MO_FOOD_WOOD_ROUTE] = {"food_wood_route", "off,bridge,craft", 0,
                            "bridge: after a pillar exit with no pickaxe, route over safe cobblestone toward a nearby "
                            "tree, clear a blocking furnace by hand, collect a log and release the turn to crafting; "
                            "craft: also do this on the surface when a table, pickaxe handle or food fuel lacks wood, "
                            "taking two logs before releasing the craft; "
                            "a privileged search hint, with ordinary player acts"},
    [MO_SH_PACK] = {"stronghold_pack", "off,trim", 0,
                    "trim, while twelve eyes wait for food, armor or two fresh swords, discard spare dirt, Nether "
                    "brick, sand, feathers, eggs, leather, a worn bow, spent pickaxes, damaged spare swords, "
                    "excess snowballs and surplus cobblestone from the inventory "
                    "screen while fewer than five slots are free for meat, a table, furnace and swords"},
    [MO_SH_ARMOR] = {"stronghold_armor", "off,full", 0,
                     "lane/chain2: full holds the stronghold latch while the player wears fewer than 15 armor points, "
                     "for at most SH_ARMOR_MAX steps; use with armor_kit=full to mine and craft the helmet before the End"},
    [MO_FURNACE_KIT] = {"furnace_kit", "off,make,carry", 0,
                        "lane/chain2: off; make, a furnace crafted from 8 cobblestone for any smelt wanted (iron ore, or "
                        "the raw meat food_cook cooks) with fuel held and no furnace held or in reach; carry, as make, and "
                        "the placed furnace broken with a pickaxe and taken back once nothing is left to smelt, before "
                        "a scout or mine can walk away (the table's carry, NETH_VEC_CARRY, for the furnace). Before "
                        "the overworld portal it also makes a replacement when needed, holds the station in the pack "
                        "and waits to light the portal until it is held. s42k4's "
                        "on_land states held 25 iron ore, coal and 400 "
                        "cobblestone and no furnace, table or wood: the furnace was left placed at the base's last smelt "
                        "before the Nether"},
    [MO_FOOD_NIGHT] = {"food_night", "off,pearl", 0,
                       "lane/chain2: off; pearl, at the pearl stage (the Nether done: blaze rods, powder or eyes held, "
                       "pearls and eyes under 12) at night in the overworld with FOOD_VIEW_MEAT meats held, no meat hunt "
                       "begun: the night is the endermen's (s42k2's pearl_7 states: the food macro 66% of the steps, "
                       "under food_hunt stock's 16 meats held, the pearl hunt 30%)"},
    [MO_DG_FOOD] = {"dragon_food", "off,fetch", 0,
                    "lane/chain2: off; fetch, in the End when the dragon macro would eat (its eat_want: food 14 or under, or "
                    "under 18 and hurt) and no food is in the hotbar, the best food of the main inventory swapped into the "
                    "hotbar (its fetch: the inventory key, a number key's click): the macro eats only from the hotbar, and "
                    "s42k2's End states held their cooked meat in the pack (food at the End's deaths 14 on average)"},
    [MO_DG_BRIDGE] = {"dragon_bridge", "off,on,wide,cliff", 0,
                      "PRIVILEGED lane/chain2 search option: during the first End island walk, inspect the server's "
                      "floor cells ahead and sneak-place carried cobblestone when the platform has a gap; wide adds a "
                      "second floor cell beside each new bridge cell; cliff (lane/dleg) is on's bridge, and where the "
                      "island's cliff stops it (the next cell west not open) a block on each side of the one-wide floor "
                      "before the rise up the cliff's face; off by default"},
    [MO_ARMOR_TIMING] = {"armor_timing", "now,after_nether", 0,
                         "lane/chain2: defer armor_kit's privileged iron-ore walk and armor craft until the player "
                         "has returned from the Nether; the full kit still runs before the pearl nights"},
    [MO_NT_DIG_FACES] = {"nether_dig_faces", "off,on", 0,
                         "PRIVILEGED chain search safety: before the Nether walker digs a block, read all six "
                         "server-side face neighbours; leave that direction if one is lava, fire or unloaded. "
                         "The ordinary visible-lava check stays in place"},
    [MO_NT_ARRIVAL_COVER] = {"nether_arrival_cover", "off,on", 0,
                             "at the arrival portal, remember a ghast seen during the look round and place a "
                             "cobblestone wall in its line of sight before walking out; wait inside the portal "
                             "when a visible opening cannot be covered"},
    [MO_NT_FAR_DIG] = {"nether_far_dig", "off,on,low", 0,
                       "PRIVILEGED: while a located fortress start lies beyond the loaded chunks, use the "
                       "sealed digger toward it when the partial path planner has no next cell; restore the "
                       "arrival portal target after each act, and clear the far digger on the rod handoff. "
                       "Low first descends toward y 55, the nearest seed-4 fortress piece's measured height"},
    [MO_FOOD_WOOD_RETRY] = {"food_wood_retry", "off,on", 0,
                            "with food_wood_route=craft, a failed errand is retried after moving 32 blocks, a "
                            "dimension change or a new surface arrival; its target is the nearest log with a "
                            "standing cell in loaded chunks, followed by the existing surface walker"},
    [MO_FOOD_WOOD_RESERVE] = {"food_wood_reserve", "off,on", 0,
                              "with strip_wood=early, gather a larger surface wood stock, craft at least 16 planks "
                              "and take or make a table before the Nether; until full iron armor, keep 16 planks "
                              "or four logs from being spent as fuel or crafting wood"},
    [MO_PEARL_SHORE_EXIT] = {"pearl_shore_exit", "off,on", 0,
                             "PRIVILEGED: on the first full-armor step after the Nether, if rock hides the sky, own "
                             "the dry surface exit once; choose the nearest loaded dry column from the height map "
                             "and follow a dug path, then release forever at dry sky or after 20 failed plans"},
    [MO_PEARL_STRIP_YIELD] = {"pearl_strip_yield", "off,on", 0,
                              "after the Nether with full armor and fewer than 12 pearls or eyes, the strip yields "
                              "before its state machine during night hours so the pearl macro can hunt"},
    [MO_WATER_DIA_STRIP] = {"water_diamond_strip", "off,on", 0,
                            "with dia_first=on, an iron pickaxe and a water bucket held before three diamonds, "
                            "let the strip mine start ahead of planks, table and stick crafts; other craft plans "
                            "keep their ordinary order, and wood crafting resumes after three diamonds"},
    [MO_WATER_CRAFT_FIRST] = {"water_craft_first", "off,on", 0,
                             "with dia_first=on, an iron pickaxe and water bucket held before three diamonds, "
                             "start a ready planks, table or sticks craft before a policy mine lock can take its turn; "
                             "the normal order resumes after that wood plan completes"},
    [MO_NT_EXIT_BACK] = {"nether_exit_backtrack", "off,on", 0,
                         "with seven rods and a failed nether_chamber, keep the Nether macro's turn within eight "
                         "blocks of the blaze spawner, following its own tunnel crumbs away before a second "
                         "portal may start; the default keeps the previous portal fallback"},
    [MO_NT_CHAMBER_SOLID] = {"nether_chamber_solid", "off,on", 0,
                             "the safe Nether chamber's floor, roof and side walls may be full, non-falling "
                             "solid blocks including fortress brick; its dug standing cell must have a blocked "
                             "ray to the blaze spawner. Off keeps the netherrack-only selector"},
    [MO_FOOD_PROVISION_EYES] = {"food_provision_eyes", "off,on", 0,
                                "when twelve pearls and eyes are held but fewer than twelve eyes are crafted, "
                                "briefly yield the exclusive food-stock stage to the powder and eye recipe; "
                                "stocking resumes as soon as the twelve eyes are held"},
    [MO_AIR_RESCUE] = {"air_rescue", "off,on,pin,out,pocket", 0,
                       "when the head is underwater and the air bar falls below half, interrupt any macro, "
                       "swim up and back toward the last dry approach, and hold that route until grounded with "
                       "air restored; the previous macro may then start again. pin (lane/flint): on, and the rescue "
                       "also ends out of water with air restored while the head is against a solid block (the held "
                       "jump keeps the feet off the ground there: on the seven flint_and_steel starts 99 of 255 "
                       "limits held the rescue so to the bound). out (lane/flint): on, and the rescue ends as soon as "
                       "the eye is out of water with air restored, on the ground or not (under pin the rest of the "
                       "held rescues bobbed at a water surface against a wall with no ceiling over the head). pocket "
                       "(lane/flint): out, and under water against a ceiling the rescue walks into a column beside "
                       "where the body fits with its eye in air, at the feet's level or one lower (seed 2's "
                       "flint_and_steel start drowned 25 of 64 attempts pinned in a lake bed's flooded shafts with "
                       "such a pocket a block aside)"},
    [MO_SNOW_TARGET] = {"snow_target", "64,32,16", 0,
                        "the held snowballs the End kit waits for: 64 as before, 32 for the measured "
                        "twenty-four-throw seed-1 fight plus eight spare, or 16 to test the current seed-1 pack "
                        "against the End; pair with the matching chain --snow-min"},
    [MO_SNOW_HEIGHT] = {"snow_height", "off,on", 0,
                        "the far snow trip first compares a nearby high Extreme Hills column with the saved "
                        "cold-biome target and walks to the nearer one; the actual snow must still be seen and dug"},
    [MO_DG_ENDER_AVERT] = {"dragon_ender_avert", "off,on,all", 0,
                           "lane/chain3: on, in the dragon macro's pit phase (the walk in, the centring, the hole), a "
                           "look that would provoke an enderman in sight (EntityEnderman.shouldAttackPlayer: within "
                           "acos(1 - 0.025 / d) of its box middle, taken at twice that cone) is turned 30 degrees "
                           "up or down off it, and no attack is held or pressed while an enderman is under the "
                           "crosshair; s1's End leg lost 13 of 64 to endermen, 9 of them stared at in the pit. "
                           "lane/dleg: all, in every phase (in place of on's rule), the stare broken before it "
                           "provokes: the step an enderman's cone (1 - 0.025 / d) would hold the look a 4th step "
                           "running, the look turned just out of it for that step (shouldAttackPlayer must hold 6 "
                           "ticks running); no attack on an enderman under the crosshair. On dc's rows 200 stares ran "
                           "6 or more, 175 in the pit phase, 69 of the 98 enderman deaths after one"},
    [MO_DG_HUNT_CAP] = {"dragon_hunt_cap", "off,on", 0,
                        "lane/legs: on, the dragon macro's crystal hunt ends (back to the seek) once its crystal has "
                        "DG_HUNTS hunts, as the hunt's pick already requires: a snowball lob with no arc from the stand "
                        "counts a hunt and returns to the hunt, whose phase change resets its step limit, so off it can "
                        "re-pick one unreachable crystal for the rest of the episode (seed 42: 41 of 43 limits on one "
                        "start, a crystal at y 86 over a stand towered 16 blocks, no snowball thrown)"},
    [MO_DG_SNOW_TOWER] = {"dragon_snow_tower", "16,24,32", 0,
                          "lane/legs: the most blocks the dragon macro's crystal hunt towers its stand for a snowball's "
                          "arc (a snowball rises about 29 blocks, so a crystal more than that over the eye is out of "
                          "reach); 16 is the default (DG_SNOW_TOWERS). Over the nine End starts' 172 hunts dragon_hunt_cap "
                          "ended, the rise wanted from the stand's base was 16 or less in 64, 24 or less in 141, 32 or "
                          "less in 171"},
    [MO_NT_FLOOR_KEEP] = {"nether_floor_keep", "off,on", 0,
                       "lane/legs: on: the Nether macro never digs the block under its own feet when the column under "
                       "that block has no floor in NT_DEEP_D cells (open, lava or not loaded: nether_safe_dig deep's "
                       "rule, which only the walker's stair digs had): the spawner tunnel's cell straight down, the "
                       "tower's way down and the crumbs' way down a pillar; the walk it has instead. Seed 42's library "
                       "Nether start with nether_spawner_tunnel on dug its own fortress deck at y 74 in 61 of 64 "
                       "attempts and fell 50 blocks into the lava sea"},
    [MO_SH_ROOM_START] = {"stronghold_room_start", "off,on,room", 0,
                          "lane/proom: on: in the overworld with 12 eyes held and the stronghold macro not latched, an "
                          "end portal frame in view within SH_RSTART_D blocks (shview's scan: what the screen shows), "
                          "or with stronghold_oracle=locate the feet within SH_RSTART_PAD of the located room's box "
                          "(the library's room starts stand in its wall, no frame in view), latches it at once, past snow_first, stronghold_food, stronghold_armor and sword_stock's "
                          "holds, and food_provision stands down while it runs: the steps before the room are behind "
                          "a player who stands in it. The library's portal-room starts (a Java save and rejoin) lack "
                          "the latch their kills had: seed 2's waited for a 32nd snowball, seed 42's for a fresh sword "
                          "and a food errand to the sky. room: on, and with stronghold_oracle=locate the way down "
                          "into the room (SO_DOWN) is over once the feet are in the located room's box: the spawner's "
                          "and the frames' steps walk from where it stands"},
    [MO_FOOD_WOOD_STUCK] = {"food_wood_stuck", "off,on", 0,
                            "lane/proom: on: the provision wood route (food_wood_route) gives up, as its own "
                            "PROVISION_WOOD_MAX cap does, once the feet have not moved a block in PROVISION_WOOD_STUCK "
                            "steps (more than the walk's longest dig by hand, SHO_DIG_BARE_FURNACE): seed 2's blaze "
                            "powder start stood up to 9,980 steps on one cell under the route, the oracle walk holding "
                            "forward against a block face or standing beside a log already gone with none held"},
    [MO_THREAT_INTERRUPT] = {"threat_interrupt", "off,view,turn", 0,
                             "lane/proom: view: in the overworld, a zombie, spider or cave spider in view (neth_rl_state's "
                             "vis) within THREAT_R blocks starts the fight's lock on it ahead of every controller that "
                             "never fights (provision, the food macro and its animal hunt, craft, the pearl's walk), "
                             "the running macro aborted, the lock ending as fight's does; turn: also a hit taken (no "
                             "fire, air and food left) with no hostile in view within THREAT_R + 1 turns the look "
                             "round for the step, as a player looks for what hit them. 66 of 127 traced deaths of the "
                             "pearl legs began with a melee hit while provision or food drove (provision_apply returns "
                             "before mac_cand: a step it takes never starts a fight)"},
    [MO_PEARL_REACH_AIM] = {"pearl_reach_aim", "off,on", 0,
                            "lane/pnight: on: pearl_oracle's in-reach branch (the census's enderman within "
                            "PEARL_ORACLE_REACH of the eye) presses attack only with an enderman under the crosshair "
                            "(neth_rl_state mo_ekind); after PEARL_REACH_BLIND in-reach steps without one there, the "
                            "branch gives the step to the oracle's own path, dig and no-path mark (pearl_reachable) as "
                            "for a target out of reach. lane/proom traced 903 of two legs' 1,182 pearl swings on stone: "
                            "the enderman 2 to 3 blocks through a cave's floor or roof, the player standing and swinging "
                            "to the bound"},
    [MO_PROV_UNDER_YIELD] = {"prov_under_yield", "off,on", 0,
                             "lane/pdig: on: food_provision's surface stage is not entered for its last reason "
                             "(provision.c PVW_UNDER: a blaze rod, powder or eye held, the feet under y 62 or under no "
                             "sky) while shore_night_yield gives the night to the pearl hunt; off, a player under 7 "
                             "food skipped the yield's PV_OFF and entered the stage, whose only move there "
                             "(strip_m_start) refuses on that same yield, and stood (act_noop) or swam in place to "
                             "dawn: on the air_rescue=out base, 23.8 and 16.8 in 100 of s2-bp's blank-night steps, 78 "
                             "of its 196 blank nights, every one with food under 7 and none held"},
    [MO_PROV_UNDER_SURFACE] = {"prov_under_surface", "off,on,hungry", 0,
                               "lane/pdig (F2a): on: food_provision's 'under' (provision_under: the surface stage's "
                               "PVW_UNDER line, the climb's want and the strip's) is the feet under y 62 only; off, "
                               "no open sky over the player (the census's) at y 62 or over counted too: on s2-eye 42.7 "
                               "in 100 of day steps were that stage under foliage or a roof at the surface, where its "
                               "only move is the strip and the hunt is not allowed, and the food fell to 5.9 by day 4 "
                               "(on, 32 attempts: worse, the stage moved under y 62). hungry: under kept, but in that "
                               "surface stage by day with the food level under 14 the food macros are allowed and "
                               "start before the strip (an eat with food held, else a hunt)"},
    [MO_PROV_WOOD_FOOD] = {"prov_wood_food", "off,on", 0,
                           "lane/pdig (F2b): on: by day with the food level under 14 and no food item held, "
                           "food_provision's craft wood route (food_wood_route=craft: provision_mode_w's PVW_CRAFT_WOOD "
                           "line and the wood step's craft) is not taken, so the food want comes first; off, the craft's "
                           "wood came before any food test: s2-bp's craft wood walk held 80 in 100 of its day steps at "
                           "food 7 to 13 with no food held, a food scout begun cut 0.80 of 0.84 a day"},
    [MO_SCOUT_CENSUS] = {"scout_census", "off,on", 0,
                         "lane/pdig (F2c): on, with food_oracle on: a food scout begun after the Nether has been seen, "
                         "in the overworld, the food level under 14 and no food item held, walks to the census's "
                         "nearest food animal (food.c food_oracle_walk: walk_to within 40 blocks, else a waypoint 32 "
                         "along the line) instead of the spiral; off, prov_want (the pre-Nether provisions' want, false "
                         "with an iron or diamond pickaxe and after the Nether) never chose that walk on a pearl start, "
                         "and the spiral swam: on s2-bp's day 2 its leg had a liquid ahead on 88 in 100 of its slow "
                         "steps, the scouts ran out of time 0.76 an attempt 48 blocks out, the animal 111 away. PRIVILEGED "
                         "(the census)"},
    [MO_SNEAK_DROP] = {"sneak_drop", "off,on", 0,
                       "lane/pdig: on: sho_follow's sneaking walk (SHO_SNEAK_F: food_provision's wood route) lets go of "
                       "sneak where the way's next cell is one to three blocks below and the landing is safe (a floor or "
                       "water under it, no lava or fire in the column, the cells down to it open; the server's blocks); "
                       "off, only food_wood_route's one-down step onto a floor let go (wood_drop) and sneak refused every "
                       "other step off a ledge: s42-eye's wood walk, forward and sneak held, stood still on 95 in 100 of "
                       "its steps, the way's next cell two or three below on 47 in 100 of those"},
    [MO_PEARL_PATROL] = {"pearl_patrol", "off,on,any", 0,
                         "lane/pdig (P2): on, with pearl_oracle, in the overworld at night with no enderman in the "
                         "macro's view: pearl_drive's census target is the nearest enderman under the sky or provoked "
                         "(pearl_target any's caves left out), and the way to it is a straight walk, not the "
                         "pathfinder: out from under a roof first by the sky dig, then at it with the cell ahead read "
                         "each step: a step up jumped, a wall or a low roof dug (a staircase up when it stands over the "
                         "eye), a drop of 1 to 3 walked off, a drop over 3, lava or water two deep turned from; at the "
                         "macro's own view the stare and the fight as they are. Off, the pathfinder's way within 48 "
                         "and a straight walk past it: on s2-eye's nights the straight walk met a wall two high on 61 "
                         "in 100 of its slow steps. any: the same walk to pearl_target's choice (with pearl_target any "
                         "the nearest of every enderman listed, caves too): on pt2-s2-eye, on saw 29 endermen against "
                         "the base's 41 and killed 11 against 16. PRIVILEGED (the census)"},
    [MO_NT_EYE_STRIKE] = {"nether_eye_strike", "off,on,reach,margin", 0,
                          "lane/rods2: on: in the Nether macro, with no foe in the vis list (whose ray goes to a mob's "
                          "middle), the nearest blaze on the screen within the fight's NT_FIGHT_D whose middle is "
                          "hidden by a block and whose eye (0.85 of its height, where its own sight of the player "
                          "starts) is not, is looked at at its eye, the player still, and struck when the crosshair is "
                          "on it; reach: only when that eye is within NT_EYE_REACH (3.3, the box then in the hit's "
                          "reach); margin: reach, and the look at the clear point of a 5x5x5 grid over its box with "
                          "the most clear neighbours (the line to the eye alone grazes the hole's rim: reach's "
                          "crosshair was on the brick 15,000 of 20,600 strike steps). The rod hunt's eight library "
                          "starts: 190,844 camp steps had a blaze at the mouth of "
                          "the hole over the camp (2.5 to 3 over the eye) seeing and shooting the player while the "
                          "macro saw none (3,977 of the camp's 18,757 health lost)"},
    [MO_NT_MOUTH_COVER] = {"nether_mouth_cover", "off,on", 0,
                           "lane/rods2: on: in the Nether macro with a spawner kept, a blaze on the screen (the vis "
                           "cone) whose eye is past NT_EYE_REACH (3.3) and within 4.5 with a clear ray to the player's "
                           "eye, the ray leaving the player's two cells through the open cell over the head (the "
                           "camp's hole): that cell filled from a face beside it, and the camp's re-dig of the hole "
                           "held 100 steps; never with a rod lying over the head. The rod hunt's eight library "
                           "starts: one block there shut every line open on 170,733 of the 190,844 camp steps a "
                           "blaze at the hole's mouth shot a player whose macro saw none (3,541 of 3,977 health)"},
    [MO_NT_DECK_WAIT] = {"nether_deck_wait", "off,on,middle", 0,
                         "lane/rods2: on: the Nether macro's walk to a kill's drop takes no step whose place lies "
                         "within 2 cells of a deck side (a cell at the feet's level with the 4 under it open) while a "
                         "client-known blaze has a clear ray to the eye (the exit cover's test); it waits crouched, "
                         "the walk's age held, at most NT_DECK_WAIT steps a drop. s1c: 26 of 64 deaths a fireball's "
                         "push off its deck into the lava sea, 20 of them in the pickup. middle: the same test, then "
                         "the standable cell of the nine round the feet farthest from a deck side walked to "
                         "crouched, the wait there"},
    [MO_PEARL_HIDE_FIGHT] = {"pearl_hide_fight", "off,on", 0,
                             "lane/pdig (F): on: with the hunt's enderman provoked (tangry), the hide (3 deep, the cover "
                             "placed over the head, held to PEARL_HIDE_OUT health) starts at PEARL_LOW_HP health or under "
                             "on the ground, its quiet (no hit for PEARL_HIDE_QUIET steps) and nothing-near tests skipped; "
                             "off, the hide waits for PEARL_HIDE_HP, a quiet and nothing within PEARL_HIDE_NEAR: on fs1's "
                             "eye legs 17 of 56 provoked endermen not killed ended with the player dead in the fight "
                             "(health 15 at the scream, 1 at the least, its own melee in 11)"},
    [MO_THREAT_SKELETON] = {"threat_skeleton", "off,hit,view", 0,
                            "lane/pnight: with threat_interrupt on, hit: a skeleton in view (vis) within THREAT_SKEL_R "
                            "after a hit taken in the last THREAT_SKEL_HURT steps starts the fight's lock on it as a "
                            "melee hostile close does, and the lock walks to a skeleton however far (a jump where a "
                            "step does not move); view: also one within THREAT_SKEL_VIEW with no hit taken. The arrow "
                            "deaths of b1-s2-bp: 11 of 11 neither closed on nor fled the skeleton, in view in 9, "
                            "provision, food or craft driving at the first hit in 8, 2 to 16 arrows over about 300 "
                            "steps"},
    [MO_NT_DIG_REFILL] = {"nether_dig_refill", "off,on", 0,
                          "lane/nleg: on: the Nether walker watches its four newest dug cells for 100 steps; one that "
                          "is open, in the eye's view, within 4 blocks, not in the player's box, with lava on a side or "
                          "top face (the lava's glow in the opened cell, lava_sense light's rule for that cell) is "
                          "filled again with the fill block (nt_seal: never dug again) before the walker does anything "
                          "else, and its column judged again. Seed 2's library Nether start: 30 of 49 deaths were the "
                          "feet cell of the search tunnel's next column dug beside unseen lava, which flowed in 10 "
                          "ticks later with the player in it"},
    [MO_NT_DIG_AIM] = {"nether_dig_aim", "off,on", 0,
                       "lane/nleg: on: a cell the Nether walker digs is aimed at its centre; when the crosshair lands on "
                       "another block instead (a stair up's head cell seen past the edge of its feet cell), the aim "
                       "moves to the centre of the cell's nearest face that the eye is outside of and whose neighbour is "
                       "open, and stays there until that cell is done. Seed 1's s1endrank10 Nether start: 61 of 64 "
                       "attempts stood at the spawner path's stair up for 26,000 steps, the attack never pressed, the "
                       "same path planned every 65 steps"},
    [MO_NT_DROP_KEEP] = {"nether_drop_keep", "off,on", 0,
                         "lane/legs's nether_floor_keep for an open cell: on: the spawner tunnel's next cell straight "
                         "under the feet, open, with no floor within 4 blocks under it, is not dropped into (the walk to "
                         "the column's middle walks off the brink uncrouched); the way is planned again without the "
                         "tunnel for the rest of the stay. Seed 42's e44b and k20 library Nether starts: the way to the "
                         "spawner stepped down off the player's own bridge at y 75 and fell 50 blocks into the lava sea"},
    [MO_NT_CAMP_SIDE] = {"nether_camp_side", "off,1,2", 0,
                         "lane/nleg (with nether_oracle spawner): 1 or 2: the camp's cell under the spawner is that many "
                         "cells to one side of its column (the first side, nearest the player when the spawner is kept, "
                         "whose room cell beside the spawner and the one over it are open), so the hole dug over the head "
                         "opens into the spawner's room; off: the spawner's own column, where the hole's top is the "
                         "spawner block. Seeds 1, 2 and 42's Nether starts: the camp stood under its spawner capped at six "
                         "blazes, none with a line to the eye, to the bound"},
    [MO_NT_FORT_PATH] = {"nether_fort_path", "off,on", 0,
                         "lane/nleg (with nether_oracle spawner): on: in the fortress phase, a kept fortress brick not "
                         "reached in NT_FORT_STALL steps is walked to along the oracle's tunnel path to the cell over it "
                         "(the server's blocks, a floor, no lava beside), as nether_oracle tunnel does; off: the walker "
                         "alone, which digs no block but the fortress's own there. Seed 2's Nether start: 14 of 64 stood "
                         "6 to 14 blocks from the kept brick, netherrack between, stepping between two cells to the bound"},
    [MO_NT_HOOD_STARVE] = {"nether_hood_starve", "off,on,rest", 0,
                           "lane/nleg: on: the Nether macro's hood (the heal walled in at the blazes) is not built, and "
                           "one built is left, while the food level is under 18 and no food the food macro would eat is "
                           "held: 1.7.10 regenerates no health under 18 food, so the hood's wait for 18 health never ends. "
                           "Seed 1's s1endrank10 with nether_camp_side 1: 19 of 64 sat hooded at 11 health and food 17 "
                           "to the bound. rest (lane/rods): and the rest (still and crouched at 6 health or under until "
                           "18) is not begun, and one begun is left, on the same rule: 21 of the 29 limits of the rod "
                           "hunt's eight library starts rested at 1 to 6 health, food 16 or 17, nothing to eat"},
    [MO_NT_BRINK_FLOOR] = {"nether_brink_floor", "off,on,lock", 0,
                           "lane/nleg: on: the Nether walker's bridge, begun with the player's centre over an open cell "
                           "(the box held on a neighbour's edge, crouched), first places cobblestone in the cell under "
                           "the centre, on the face of a solid neighbour that borders it; off: the look goes to the side "
                           "of the block under the centre's column, which is air, so no placement lands. Seed 42's e44b "
                           "and k20 and seed 1's s1cont150 Nether starts with nether_drop_keep: 33 of 192 stood at the "
                           "brink of their own bridge to the bound, the look swinging 39 to 82 degrees down. lock: on, "
                           "and a fight lock of any kind held out of reach while the player does not move is dropped "
                           "as a blaze's is (k20: a wither skeleton across the gap, the player crouched at its "
                           "bridge's corner for 14,000 steps)"},
    [MO_NT_PICK_UP] = {"nether_pick_up", "off,on,dig", 0,
                       "lane/rods: on: the Nether macro's walk to a killed blaze's drop, when the floor under the place "
                       "the blaze was last seen lies 2 or more blocks over the player's feet (a rod lying out of the "
                       "pickup's reach), goes by the walker toward that cell (it digs no fortress brick at the camp), "
                       "at most 300 steps; off: the crouched forward walk toward it, 60 steps. The rod hunt's eight "
                       "library starts under the bench set: 791 of 1,074 rods dropped lay 2 to 4 blocks over the feet "
                       "(the spawner's platform over the camp cell), 332 of them picked up within 600 steps; dig: on, and a "
                       "drop resting 2 to 4 over the feet on a block whose column down to the feet's level is open is "
                       "reached by walking under it and breaking the block it rests on (not bedrock, a spawner, a "
                       "portal or a block beside lava), so it falls to the feet; and a drop 3 over the feet in a cell "
                       "beside the feet's column, over a solid one, has the block it rests on broken from where the "
                       "player stands (never the floor under the spawner), so it falls one block, into the pickup's "
                       "reach. The rod hunt's eight starts: reached 28 -> 50 of 512, rods gained 1,053 -> 1,218"},
    [MO_NT_HOOD_ROOF] = {"nether_hood_roof", "off,skip", 0,
                         "lane/rods: skip: the Nether macro's hood is not begun where its roof cannot go in: the cell "
                         "over the player's head is open and so are the cell over it and the four beside it (a block "
                         "is placed only against a solid face). The rod hunt's eight library starts under "
                         "nether_near_stop and nether_hood_starve rest: 34 of 83 hood deaths took most of their hits "
                         "with the roof open, the cell over it air on about 96% of those steps"},
    [MO_OB_YIELD_LIVE] = {"obsidian_yield_live", "off,on", 0,
                          "lane/bportal: on: obsidian_yield's veto on the strip (e->ob.yield, set by the last obsidian "
                          "search) holds only while the obsidian macro is wanted. Off, a yield set before the kit's 20th "
                          "obsidian is never cleared (strip_want0 returns on the provisions' want before ob_search runs), "
                          "and the strip's climb to the surface (strip2's ST3_OUT) is refused for the rest of the run: "
                          "seed 2's obsidian start, 65% of its provision wait's rows under y 40. Kept by the "
                          "orchestrator on the count (the wait's rows under y 40 46 -> 21 in 100, the climb owning "
                          "them); reached under the gap (seed 2 0 -> 1 of 64 alone)"},
    [MO_FOOD_SCOUT_SKY] = {"food_scout_sky", "off,on,foliage", 0,
                           "lane/bportal: on: with food_oracle's provisions wanted, the provisions' scout begins only "
                           "with the feet under the sky (the census's pl_sky, PRIVILEGED as its animal is), and the "
                           "strip's climb (strip2's ST3_OUT) goes on past y 62 until they are. Off, the scout began in "
                           "the strip's stair at y 58 or over: seed 2's obsidian start, 88% of the scout's rows with no "
                           "sky over the head, still on 65% of them, 10 of 23 scouting attempts never 16 blocks from "
                           "where they began. Kept by the orchestrator on the count (scouts out of time 1.3 -> 0.0 a "
                           "10,000 wait steps, every scout ending at an animal); reached under the gap, with "
                           "obsidian_yield_live: seed 2 0 -> 7 of 64 with food_oracle on. foliage: the same, the sky "
                           "read through leaves (shoracle_sky_here over the eye) where on reads the height map, which "
                           "leaves cover: under on, 5,203 of seed 2's surface wait rows with an animal within 64 and "
                           "none in view stood under a tree, the scout refused and the climb holding them"},
    [MO_MINE_YIELD] = {"mine_yield", "off,on", 0,
                       "lane/bportal (a driver what-if, not judged): on: the policy's attack starts no mine lock while "
                       "a controller wants the step: food_oracle's provisions (prov_want), the food scout running, a "
                       "hunt, or the bucket portal wanted (bp_want); a lock already running is not cut. Seed 2's "
                       "obsidian start, the provision wait over obsidian_yield_live, food_scout_sky and "
                       "food_wood_stuck: the mine kind's net descent -619 -> +20 blocks, reached 7 -> 8 of 64"},
    [MO_OB_FB_GAP] = {"obsidian_fallback_gap", "320,640,1280,2560", 0,
                      "lane/bportal: obsidian_carry fallback's wait: the steps with 10 or more obsidian held in the "
                      "overworld and none gained before the first frame is built at 10; 320 is about the 90th centile "
                      "of the gaps between gains past 10 held under carry on in the attempts that reached 20 from the "
                      "four obsidian starts (667 gaps: p50 210, p90 325, p95 574)"},
    [MO_CLIMB_LAND] = {"climb_land", "exact,cell,centre", 0,
                       "lane/bportal: climb_way pillar's level done when the feet land a block up: exact, within 0.01 "
                       "of the jump's base + 1; cell, the feet's block one over the base's (a column stood on at y "
                       "n.0156 put every base off the grid by that much, the block placed under the last level landed "
                       "the feet at n + 1.0000, and the level waited out its 160 steps; on seed 2's obsidian start "
                       "under obsidian_carry fallback the stair then gave up on the pillar's top at y 34 and 29 of 40 "
                       "deaths were the policy's fall from it); centre, cell's test and the feet kept over the "
                       "column the level began on (the move keys along the look and across it), the jump begun only "
                       "within 0.18 of its middle on both axes (0.29 off, the 0.6-wide body reached the next column, "
                       "whose block over the head stopped every jump at y 34: the level given up on its jumps)"},
    [MO_SNOW_GATE] = {"snow_gate", "off,start", 0,
                      "lane/bportal: start: the snow trip's start (snow_kit's, rank 19) held to the rules its cand "
                      "already has: with the pearl macro on, not before twelve pearls and eyes are held, and "
                      "snow_heal's; off, as before, the start ran whenever another macro's cand opened the start "
                      "loop. Seed 1's obsidian start (no pearl held): the trip begun between obsidian jobs, its "
                      "night stand at y 10 to 11 spun level under no sky until the limit, 23,679 of the 28,751 "
                      "mine-labelled trace rows in 17 of 64 attempts"},
    [MO_FOOD_PROV_COOK] = {"food_prov_cook", "off,on", 0,
                           "lane/bportal: on: while food_oracle's provisions are wanted (prov_want: the portal's kit "
                           "held, under 40 food points), the smelt takes the largest raw meat stack before any iron "
                           "ore, as PV_STOCK's does, and its step cap counts the meat (FOOD_COOK_N raw held). Seed 2's "
                           "obsidian start: the wait's smelts ran on the strip's iron ore on 71 in 100 of their rows "
                           "(the fuel spent on it, the 40-point bar counting raw pork at 3 against cooked 8)"},
    [MO_STRIP_SPIN] = {"strip_spin", "off,water", 0,
                       "lane/flint: water: a strip run (strip.c) that has walked no cell in STRIP_SPIN_AGE steps of "
                       "its own no longer takes the quarter turn at water the liquid ray sees within 3 blocks (lava "
                       "still turns it, and the floor's wet test still holds). Seed 42's s42k20 flint_and_steel "
                       "start: 44 of 63 limits stood at y 16 in one cave beside a spring, the strip turning at water "
                       "in all four directions every 16 steps, no cell walked, for up to 80,000 steps"},
    [MO_INWALL] = {"inwall", "off,dig", 0,
                   "lane/flint: dig: a full cube (stone, dirt, sand, gravel, the ores; not bedrock or obsidian) at any "
                   "of the eye's eight suffocation points is dug with the best tool, the look on its middle, before "
                   "any macro or the policy acts; the macro in hand drives again once the head is clear. The seven "
                   "flint_and_steel starts: 24 of 175 deaths were the in-wall drain, gravel fallen into the head's "
                   "cell while the obsidian or strip climb stood under it"},
    [MO_LAVA_EDGE] = {"lava_edge", "off,sneak,raw", 0,
                      "lane/flint: sneak: on the ground with lava for the floor of a cell beside the feet (the eight "
                      "round them and their own), or under open air within 3 blocks there, every act (a macro's or "
                      "the policy's) holds sneak, which keeps a walk from going over the edge. The seven "
                      "flint_and_steel starts: 38 of 56 fire deaths began with a fall, under every owner. raw: only "
                      "the act no macro drove, which also lets go of jump (sneak holds no edge in the air): 18 of "
                      "53 fall-first fire deaths were the policy's own keys there, 15 with jump held"},
    [MO_WATER_EXIT] = {"water_exit", "off,step", 0,
                       "lane/flint: step: with no macro in hand and the eye in water, the walk (with the jump for a "
                       "step up) into a face neighbour's column where the body stands on a solid floor with its eye "
                       "in air, at the feet's level or one up. Seed 1's s1cont150 flint_and_steel start under "
                       "air_rescue=out: 50 of 60 limits stood with no macro in one flooded hole beside a tunnel, every "
                       "macro refusing to start with the eye in water"},
    [MO_BP_WALK] = {"bportal_walk", "off,dig", 0,
                    "lane/flint: dig: the bucket portal's water trip to a remembered source more than 3 columns off or "
                    "not one block under the feet begins as obsidian_walk dig's walk (the strip tunnels to it, a stone "
                    "pickaxe or better held), the trip itself on arrival. The flint_and_steel starts: on s42k50 its "
                    "straight walk gave up (no half-block nearer in 160 steps) in 1,224 of 1,262 water trips"},
    [MO_LAVA_ESCAPE] = {"lava_escape", "off,seal", 0,
                        "lane/flint: seal: the overworld lava escape first places cobblestone (from the hotbar) into "
                        "the lava cells against the body a block may go into, over the head and at the head's level "
                        "first, then aims at the nearest standable face neighbour (a solid floor, its two cells clear "
                        "of blocks and lava, at the feet's level or one up) instead of the last cell stood on when that "
                        "is overhead, else pillars (jump, a block placed under the feet). The seven flint_and_steel "
                        "starts: of 40 traced fire deaths that had poured their water, the escape aimed overhead in "
                        "39 and the head's cell was lava in 33 (no liquid lift then)"},
    [MO_DG_CLUTCH] = {"dragon_clutch", "off,water", 0,
                      "lane/dleg: water: the dragon macro keeps a water bucket in the hotbar (one fetch from the main "
                      "inventory), and a fall (airborne, going down, DG_CLUTCH_FALL blocks under the highest point "
                      "since the ground) is caught: the look straight down, the water bucket in hand, the use pressed "
                      "once the crosshair meets a block's top face (the water placed over the ground the feet land "
                      "on: EntityPlayerMP's handleFalling reads the water before the fall's damage), then the water "
                      "taken up again with the empty bucket. The eight End starts of library-kills under the flint "
                      "bench set: the dragon's pass throws the player from the surface 15 to 60 blocks up and the "
                      "fall onto end stone is the largest class of the deaths"},
    [MO_DG_HOLE_FLOOR] = {"dragon_hole_floor", "off,on", 0,
                          "lane/dleg, PRIVILEGED (the server's End blocks): on: the dragon macro's pit hole digs the "
                          "block under the feet only while a block is under that one (read off the server's End "
                          "blocks as dragon_bridge does), else the pit is over. Off, a pit entered on the island's "
                          "bottom layer (y 12-17 near the centre) dug through it: 18 of the 122 void deaths of the "
                          "eight End starts of library-kills"},
    [MO_DG_VOID_EDGE] = {"dragon_void_edge", "off,sneak,raw", 0,
                         "lane/dleg, PRIVILEGED (the server's End blocks): in the End on the ground with a cell beside "
                         "the feet open and no block under it within the fall the body survives (health + 2 blocks), "
                         "sneak: every act sneaks (the macros' walks too: no step off the island's rim or the bridge; a walk only: an act holding a move key), "
                         "raw: only the act no macro drove, its jump dropped (lava_edge raw's rule with the void as the "
                         "hazard). Off, 24 of the 122 void deaths of the eight End starts of library-kills were a walk "
                         "off the rim or the bridge with no push and no hit"},
    [MO_DG_FOLLOW] = {"dragon_follow", "off,server", 0,
                      "lane/dleg, PRIVILEGED (the server's dragon): server: the dragon macro's follow (the dragon out "
                      "of view DG_LOST steps) walks toward the server's dragon's place, kept within DG_FOLLOW_FAR of "
                      "the centre, until the client holds its copy again (at most DG_HUNT_STEPS), instead of its last "
                      "place in view. At render distance 4 the dragon stops where its chunks are not all loaded; off, "
                      "61 of the 65 bound-limited attempts of the eight End starts of library-kills stood by the "
                      "centre with no dragon copy for their last 5,000 steps"},
    [MO_DG_WAIT] = {"dragon_wait", "ring,centre,band", 0,
                    "lane/dleg: where the dragon macro's box (the pit the charge is waited in) is made: ring, within "
                    "DG_PIT_R (20) of the centre, the first cell in from the edge; centre, walked to the centre and "
                    "made within DG_WAIT_R (8), and a box short of it left for the centre once the dragon is out of "
                    "view DG_UNBOX steps. The charge throws the body 20-50 blocks: with dragon_follow server, 143 of "
                    "the 160 void deaths of the eight End starts of library-kills were the dragon moving the body, "
                    "and laid from the centre 103 of the 115 throws among them land on the island; band (privileged), "
                    "the box within DG_WAIT_R of a point DG_BAND_R (16) out toward the island's widest eighth (the "
                    "server's End blocks): the charges at r 8-30 dealt 2.56 for each health lost, the centre's 2.23"},
    [MO_DG_LATE] = {"dragon_late", "off,nohunt,nopit,walkin,all", 0,
                    "lane/dleg: once the client holds no crystal copy (the dragon's copy held, past DG_LATE_STEPS): "
                    "nohunt, no hunt (the kept list's crystals it never saw blow up); nopit, no duck and no hunt's hole "
                    "out past DG_WAIT_R; walkin, the wait for the pass out there a walk in to the centre (a box out "
                    "there left); all, the three. With dragon_wait centre, after the crystals 31% of the steps were "
                    "at r 30+ (seek 58% of them, pits 18%, hunts 11%) and 63 void deaths came there"},
    [MO_DG_PIT] = {"dragon_pit", "dig,once,stand", 0,
                   "lane/dleg: the pit phase's hole (DG_HOLE_DEPTH under the feet, against the wing push): dig, every "
                   "entry digs one from the feet it finds; once, an entry into the hole it dug last digs nothing (the "
                   "ratchet: follow, seek and pit again sank stands 6 to 23 blocks at r 8); stand, no hole (the walk "
                   "in kept, the duck stands on the floor and blocks in the seek). On dw's rows the deep holes dealt "
                   "less a charge (ratio 0.72 to 1.41) and the pit phase held 46 of 134 void deaths"},
    [MO_THREAT_END] = {"threat_end", "off,ender", 0,
                       "lane/dleg: with threat_interrupt on, ender: in the End (where threat_interrupt's own test never "
                       "runs: the overworld only), an enderman in view within 4 after a hit taken in the last 20 steps "
                       "starts the fight's lock on it, a sword in hand first, unless the dragon's head is within 12 "
                       "(the lock ends there too: the charge comes first); a hit with no enderman in view and the "
                       "dragon not within 24 turns the look round. On dw's rows the 96 enderman deaths took 11.8 hits "
                       "each over 150 to 400 steps, attack pressed in 4 of them"},
    [MO_PEARL_MARCH] = {"pearl_march", "off,on", 0,
                        "lane/pground (orders 9:23 PM): on, with pearl_oracle, in the overworld at night with no census "
                        "enderman within PEARL_MARCH_NEAR (64, across) or none reachable: the hunt marches, a straight "
                        "walk (pearl_patrol's cell-ahead walk, sprinting on the open ground) to the end of a line through "
                        "the place of the episode's first march step along the yaw faced then, PEARL_MARCH_HALF (500) "
                        "each way, out and back between its ends; out from under a roof first by the sky dig. While "
                        "fed (food PEARL_MARCH_FED and PEARL_MARCH_FOOD food points held) at night in the overworld, "
                        "provision is off and the hunt yields only to a fight in the last 100 steps and the strip's climb "
                        "out. A near one (within 64) is the oracle walk's as before. PRIVILEGED (the census, the "
                        "server's blocks)"},
    [MO_THREAT_ENDER] = {"threat_ender", "off,reach,low", 0,
                         "lane/pground (orders 12:33 AM): reach, in the overworld: an enderman screaming (the client's "
                         "own flag) within THREAT_ENDER_R (3.0) of the eye, found in the client's entity list whatever "
                         "the look (neth_rl_far's near: the nearest within 4, no cone, no line test), starts the fight lock "
                         "on it before any macro drives the step, a running one aborted and a lock on another living "
                         "moved to it; the lock aims at it and swings on the crosshair at its 10-step rate from that "
                         "reach when the view list has dropped it. Reads only the client. low: reach with the lock's "
                         "look on an enderman 0.4 over its feet, not its middle (EntityEnderman.shouldAttackPlayer "
                         "stares at the box middle, and one stared at within 4 teleports at random)"},
    [MO_PEARL_FLOOR] = {"pearl_floor", "off,watch", 0,
                        "lane/pground (orders 1:49 PM, the prebuilt floor screen; a WHAT-IF's rule, never a benchmark's): "
                        "watch, in the overworld before anything else drives (no other macro starts; provision, the "
                        "shore and threat_interrupt passed by): the cell of the episode's first step held; a hostile "
                        "within 3.2 of the eye (the client's nearest enderman within 4 whatever the look, 0.4 over its "
                        "feet; any other in the view list) looked at and hit on the crosshair every 10 steps with the "
                        "best weapon; eating (the food macro's); after the stats screen's endermen killed rises, a walk "
                        "to the target's last place (60 steps at most), 10 steps there, and back; an enderman in view "
                        "(within 64, a clear line) not screaming stared at (80 steps, then left 600), a screaming one "
                        "watched 2 under its feet; else a sweep 30 degrees either side of the first step's yaw, its "
                        "phase and an 8-degree look noise from the policy's camera bins, and at the start a walk out "
                        "along that way of 2 steps a yaw bin (0 to 80) then back (the sampler's: an idle rule is one "
                        "trajectory otherwise). Reads only the client"},
    [MO_ARMOR_GIVEUP] = {"armor_giveup", "ring,all", 0,
                         "lane/supply: armor_kit's iron ores given up (the dig walker found no way): ring, the last 8 "
                         "(an ore past them is tried again); all, every one (up to AR_BAD_ALL) until the macro digs an "
                         "ore or the feet move AR_BAD_MOVE blocks from where the list began. armor_timing=now on the "
                         "seven flint_and_steel starts: 81 of 448 attempts stood in one cell 10,000+ steps under the "
                         "armour macro, nine ores of one site unreachable cycling through the ring of 8"},
    [MO_DIG_WADE] = {"dig_wade", "off,on", 0,
                     "lane/supply: on: the dig walker (dig.c: armor_kit's ore walk, snow_kit's) whose plan finds no way "
                     "while the feet stand in water over a solid floor with the head in air walks, through such cells "
                     "on the feet's level (DGW_WADE_R across at most), to the nearest cell whose feet are dry, then "
                     "plans again; off, it stands (the tunnel planner refuses any cell with water beside it). "
                     "armor_timing=now on s1cont150: 41 of 64 attempts stood in a flooded floor (y 61 water over "
                     "stone) to the bound under the armour macro"},
    [MO_DIG_BRIDGE] = {"dig_bridge", "off,on", 0,
                       "lane/supply: on: the dig walker (dig.c) whose next cell's floor is air (the tunnel planner's "
                       "bridge, NV_TUN_OPEN) places it, cobblestone from the hotbar against a solid face (nether.c "
                       "nt_cover_place), and keeps the plan; off, it plans again (the same way: armor_timing=now on "
                       "s42k20, 14 of 64 attempts stood under the armour macro to the bound)"},
    [MO_ARMOR_REACH] = {"armor_reach", "off,on", 0,
                        "lane/supply: on: armor_kit's ore within 4 blocks of the eye, not in the feet's column, is "
                        "looked at and, while the crosshair is on it, dug from where the player stands (the best "
                        "tool); three looks that land elsewhere give the reach up for that ore and the dig walker "
                        "goes on; off, the walker digs its way into the ore's cell (the planner refuses a cell with "
                        "sand or gravel over it: armor_timing=now on s1snow32full100, 18 of 64 attempts stood)"},
    [MO_CARRY_GATE] = {"carry_gate", "hold,release", 0,
                       "lane/supply: furnace_kit=carry holds the overworld portal (the frame and the bucket portal) "
                       "until a furnace is held; hold, always; release, only while one can be had: a placed furnace "
                       "in reach, or 8 cobblestone held with a table held or in reach, 4 planks or a log; with none, "
                       "the portal goes ahead without it (craft.c carry_furnace_gate). fxc on the seven flint starts: "
                       "21 limits held 10+ obsidian, 14 of them with no furnace and the bucket portal refused for it "
                       "(s1snow32full100 10)"},
    [MO_ARMOR_TURN] = {"armor_turn", "off,station,make", 0,
                       "lane/supply: station: armor_kit's turn ahead of the strip mine (strip and strip2) while an "
                       "iron pickaxe is held, pieces are still wanted and a furnace and a table are each held or in "
                       "reach (the macro's near_furnace / near_table, no other read): tried before the strip mines in "
                       "the start order, and a strip run ended on the armour's scan step when an ore is found (armor.c "
                       "arm_turn); off, the armour starts last. fb/fa on the iron starts to flint_and_steel: the "
                       "armour drove 61 of 320 attempts, one wore a set. make: station's turn with the station as it "
                       "can be made from what is held: a table from 4 planks or a log, a furnace from 8 cobblestone "
                       "with a table (iron_plan makes it once the first ore is in hand); ap: 13 of the 14 armour "
                       "starts begin with no furnace, and 49 of the 66 limits that never took the turn never had one"},
    [MO_ARMOR_PICK] = {"armor_pick", "off,fetch", 0,
                       "lane/supply: fetch: armor_kit's mine, when no pickaxe in the hotbar can dig its ore (iron "
                       "ore a stone one, coal ore a wooden one) and the main inventory holds one, swaps the best of "
                       "them into the hotbar slot the macros need least by a number key's click under the inventory "
                       "screen (snow.c sn_fetch's rule; armor.c arm_pick_fetch); off, the walker digs with what the "
                       "hotbar holds. al on s1cont150 iron_pickaxe: the hotbar's wooden and stone pickaxes worn out, "
                       "the iron one in the pack, every block after dug by hand with no drop: 24 ores dug, 8 held, "
                       "64 of 64 attempts stopped short of 24 iron"},
    [MO_ARMOR_TABLE] = {"armor_table", "off,take", 0,
                        "lane/supply: take: while armour pieces are wanted, none held and no piece craftable now, a "
                        "placed table in reach ends the armour mine's run and is taken by the craft macro's C_TAKE "
                        "(the carry's, NETH_VEC_CARRY; at mac_start's head as furnace_kit=carry's furnace is) before "
                        "the mine walks away; the craft places it again for the pieces (armor.c arm_table_take). ap "
                        "(armor_pick=fetch): 122 of 167 limits holding 24 ingots and no boots had held a table and "
                        "had one in reach, and at the end had neither (81 last left a placed one under the mine)"},
    [MO_THREAT_SCREEN] = {"threat_screen", "off,close", 0,
                          "lane/supply: close: with threat_interrupt on, a threat it would fight (a melee hostile "
                          "within its reach, or threat_skeleton's) while the furnace's screen is open closes the "
                          "screen that step, the running macro ended, and the fight takes it on the next (the "
                          "furnace keeps smelting; the craft opens it again for the output); off, threat_step does "
                          "nothing under any screen (fight.c). The armour leg: every first hit in C_SMELT's wait "
                          "(ap 6, armor_table=take 15) had the furnace's screen open"},
    [MO_ARMOR_CRAFT] = {"armor_craft", "off,first", 0,
                        "lane/supply: first: an armour piece craftable now (armor.c arm_plan: its ingots held and a "
                        "table held or in reach) starts the craft macro with that plan ahead of every other start "
                        "(mac_start, after furnace_kit=carry's take), and ends an armour or strip mine's run on its "
                        "scan step for it; a start that crafted nothing waits AR_CF_RETRY steps before the next "
                        "(armor.c arm_craft_first). tm: 123 of 185 attempts stopped at 24 ingots with no boots had "
                        "rows with a piece craftable while another macro owned (the armour mine 6,687 rows, the "
                        "obsidian 1,771, the strip 905), and 150 ended with no table held or in reach"},
    [MO_ARMOR_IPICK] = {"armor_ipick", "off,first,keep", 0,
                        "lane/supply: first: while no iron or diamond pickaxe is held, armor_kit's want (armor.c "
                        "arm_need) counts an iron pickaxe's 3 ingots and an armour piece waits (arm_plan) while 3 "
                        "ingots are held, so craft_plan reaches iron_plan's iron pickaxe first; keep: first, and the "
                        "armour mine does not dig while the pickaxes that dig iron ore are only iron or diamond and no "
                        "stone pickaxe can be made from what is held (arm_pick_last). sf (the armour base on the flint "
                        "leg): 238 of 448 attempts ended with no pickaxe of any kind and the diamond pickaxe rung fell "
                        "323 -> 156"},
    [MO_DIG_TOOL] = {"dig_tool", "best,cheap", 0,
                     "lane/armed: cheap: the dig walker (dig.c dgw_step: the armour mine's and the snow macro's tunnel) "
                     "and the armour reach (armor.c) dig a block a pickaxe mines with the cheapest hotbar pickaxe that "
                     "mines it (strip2.c st_rank: stone, then wood, then iron and diamond), as the strip does; best "
                     "(the default), the highest-tier one (common.c best_tool). The 28 armoured starts lane/armed "
                     "earned from the armour leg hold an iron pickaxe with a median 77 of 250 uses (the library's "
                     "iron starts 250), and on the legs after them 557 of 1,792 attempts lose their last pickaxe "
                     "and never make one again"},
    [MO_SPICK_KEEP] = {"spick_keep", "off,first,make", 0,
                       "lane/armed: first: while st_spick_want (strip2.c: a stone pickaxe kept beside an iron one) "
                       "wants one and its 3 cobblestone, 2 sticks and a table held or in reach are there, the stone "
                       "pickaxe is craft_plan's plan ahead of the armour's, the iron's and the late plans (craft.c); "
                       "make: first, and with no table held or in reach a table made from 4 planks for it (the carry "
                       "macro takes it back); off (the default): the stone pickaxe after those plans and never a "
                       "table made for it. On lane/armed's dn legs, with an iron pickaxe and no stone one held and "
                       "the stone pickaxe's makings and a table there, the plan was a smelt, a bucket or a flint and "
                       "steel on 146,343 trace rows against the stone pickaxe's 67,799, and the mine's and strip's "
                       "stone wore the iron pickaxe out (an earned start also knows no table: 504 of 1,792 attempts "
                       "began with the planks, sticks and cobblestone and none)"},
    [MO_WOOD_BARE] = {"wood_bare", "off,climb", 0,
                      "lane/armed: climb: strip2's wood errand (st3_wood_want: no pickaxe past the stone pickaxe's "
                      "stage wants the table's and the pickaxes' wood) climbs out with no pickaxe while 8 "
                      "cobblestone are held, as the provision's bare-handed exit does (st3_pillar's bare_exit: the "
                      "ceiling dug by hand, its top and step time); off (the default): no climb without a pickaxe, "
                      "only a log in view walked to. On lane/armed's an legs 406 of 1,792 attempts ended at the limit "
                      "underground with no pickaxe, no wood to make one and no macro owning the step"},
    [MO_NT_HOOD_KEEP] = {"nether_hood_keep", "off,on", 0,
                         "lane/armed: on: healing in the hood (hood 2), the feet out of its cell or one of the cells "
                         "the build placed open again (air or fire): back to the build, which places only the open "
                         "cells (the feet more than a block off its cell or at another height: a new hood where the "
                         "player stands), at most NT_HOOD_KEEP times a hood; off (the default): the heal goes on, "
                         "standing crouched wherever the player is. On lane/armed's k50 a52 entry start 28 of 55 "
                         "deaths were in a hood with a cell open or with the feet knocked out of it"},
    [MO_ARMOR_CHOICE] = {"armor_choice", "armor,ipick,check,invalid", 0,
                         "lane/choice: the armour craft slot's selector (armor.c arm_choose; the slot is armor_craft="
                         "first's). Its offers: the next armour piece, and beside it a replacement iron pickaxe on a "
                         "conflict (the one iron pickaxe held damaged, no other iron or diamond one, the craft could "
                         "make an iron pickaxe now, and the ingots cannot pay for the piece and 3 more). armor (the "
                         "default): the piece, today's choice bit for bit; ipick: the pickaxe on a conflict; check: "
                         "armor, the query run twice and every env byte and the offers compared (books); invalid (a "
                         "test's): a selection never offered, the default committed and counted. Books: neth_vec_kit's "
                         "ch_* (chain --choice-log)"},
    [MO_WOOD_SEARCH] = {"wood_search", "off,look,walk", 0,
                        "lane/wood: with no pickaxe of any kind, no cobblestone, ore or ingot and too little wood for "
                        "the first pickaxe, a log in view (the cast's seen record) is the seek's (its walk and dig); "
                        "none in view: look, a turn on the spot (10 degrees a step, 36 steps), again after 600 steps; "
                        "walk, the turn and then legs of a widening square spiral (24 blocks, the k-th 24 (1 + k / 2)), "
                        "a turn at each leg's end, a drop over 3 or lava ahead turned from, water swum across; nothing "
                        "dug (mods/macros/wood.c). lane/fwd's forward panel: 23 of 64 worlds made no wooden pickaxe"},
    [MO_SEEN_LOG2] = {"seen_log2", "off,on", 0,
                      "lane/wood: on: the cast's seen log (neth_rl_state seen kind 3) is block 17 or 162 (acacia and dark "
                      "oak), the host cast only (the device cast keeps 17); off, block 17 alone"},
    [MO_LAVA_HIT] = {"lava_hit", "off,seen", 0,
                     "lane/lava: seen: the lava escape's hit (strip.c, a hit ends the running macro for the escape) is "
                     "also the screen's fire with any health lost since the step before and lava (source or flowing) in "
                     "view within 3 of the eye (mods/macros/lavahit.c); off (the default), only burning with 3 or more "
                     "lost in one step. dev128's base (lane/wood's A1): 9 of 21 lava deaths wore 15 armour points, lost at "
                     "most 2.2 in a step and never began the escape"},
    [MO_BP_FILL_GIVEUP] = {"bportal_fill_giveup", "off,rest,sky", 0,
                           "lane/portal: the bucket portal's fill trips (a bucket to water or lava remembered) that "
                           "end with no bucket filled are counted, a fill zeroes the count, and BP_FILL_STREAK in a "
                           "row: rest, the macro rests BP_FILL_LONG steps (a held site kept); sky, the kind's "
                           "remembered sources are forgotten and for BP_FILL_BLIND steps only the eye's are kept "
                           "(bportal_oracle's are not), so with none the climb to the sky and the walk over the "
                           "surface (bportal_roam) look for one. Only the macro's own books are read. off (the "
                           "default): each given-up source rests BP_FILL_REST steps and the next trip begins the step "
                           "after. On lane/wood's dev128 base 15 of the 28 worlds alive at the limit with the portal's "
                           "makings spent half or more of the steps after their last milestone in the fill trip, "
                           "never filled (14 of them 89% or more)"},
};
