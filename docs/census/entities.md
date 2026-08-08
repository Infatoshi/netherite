# Entity census

Oracle registry count: **81** (`EntityList.init()` registrations).

Census row count: **81**.

The census uses primary tape JSONL files only, excluding geometry and snapshot sidecars. Repeated timestamped recordings are collapsed to their scenario name. When `docs/SCOPE.md` cuts simulation but magma still has a material render path, the row is `partial(render-only; simulation cut)` rather than hiding that implementation as `cut`.

## Hostile mobs

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| elder_guardian | EntityList.java:4 | cut | - | none | - |
| wither_skeleton | EntityList.java:5 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | scenario_wither_skeleton | - |
| stray | EntityList.java:6 | partial(render-only skeleton variant; no distinct simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entity_skin_for_name | none | `/summon minecraft:stray ~ ~ ~` beside a skeleton and record an arrow volley |
| husk | EntityList.java:23 | partial(render-only zombie variant; no distinct simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entity_skin_for_name | none | `/summon minecraft:husk ~ ~ ~` beside a zombie in daylight |
| zombie_villager | EntityList.java:27 | partial(render-only zombie stand-in; distinct model is cut) | magma/game/entity_render.c:gm_entity_type_for_name | fast_s0_survival_default_rd8 | - |
| evocation_illager | EntityList.java:34 | cut | - | none | - |
| vex | EntityList.java:35 | cut | - | none | - |
| vindication_illager | EntityList.java:36 | cut | - | none | - |
| creeper | EntityList.java:50 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| skeleton | EntityList.java:51 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| spider | EntityList.java:52 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8 | - |
| giant | EntityList.java:53 | missing | - | none | `/summon minecraft:giant ~ ~ ~` on a marked flat pad |
| zombie | EntityList.java:54 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon, scenario_ender_dragon_demo, scenario_smoke_zombie | - |
| slime | EntityList.java:55 | partial(simulated and rendered; gel translucency remains open) | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_slime_gel_emit | none | `/summon minecraft:slime ~ ~ ~ {Size:2}` on a contrasting floor |
| ghast | EntityList.java:56 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:ghast ~ ~5 ~` against open Nether sky |
| zombie_pigman | EntityList.java:57 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | scenario_pigmen_aggro | - |
| enderman | EntityList.java:58 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | scenario_enderman_fight | - |
| cave_spider | EntityList.java:59 | partial(render-only spider variant; no distinct simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entity_skin_for_name | none | `/summon minecraft:cave_spider ~ ~ ~` beside a spider in a lit pen |
| silverfish | EntityList.java:60 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:silverfish ~ ~ ~` beside stone-brick variants |
| blaze | EntityList.java:61 | partial(live AIFireballAttack state machine not ported) | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | scenario_blaze_bow, scenario_blaze_bow_demo, scenario_blaze_melee | - |
| magma_cube | EntityList.java:62 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:magma_cube ~ ~ ~ {Size:2}` on a marked Nether floor |
| ender_dragon | EntityList.java:63 | implemented | magma/game/dragon_live.c:gm_dragon_tick; magma/game/entity_render.c:gm_entities_emit | scenario_dragon_kill, scenario_dragon_kill_geared, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| wither | EntityList.java:64 | cut | - | none | - |
| witch | EntityList.java:66 | partial(render-only; no live witch simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8 | - |
| endermite | EntityList.java:67 | missing | - | none | `/summon minecraft:endermite ~ ~ ~` in a glass pen |
| guardian | EntityList.java:68 | cut | - | none | - |
| shulker | EntityList.java:69 | cut | - | none | - |

## Passive and neutral mobs

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| skeleton_horse | EntityList.java:28 | cut | - | none | - |
| zombie_horse | EntityList.java:29 | cut | - | none | - |
| donkey | EntityList.java:31 | cut | - | none | - |
| mule | EntityList.java:32 | cut | - | none | - |
| bat | EntityList.java:65 | partial(render-only; no live bat simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon | - |
| pig | EntityList.java:90 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:pig ~ ~ ~` in a small marked pen |
| sheep | EntityList.java:91 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| cow | EntityList.java:92 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:cow ~ ~ ~` in a small marked pen |
| chicken | EntityList.java:93 | implemented | magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| squid | EntityList.java:94 | partial(render-only; no live squid simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8 | - |
| wolf | EntityList.java:95 | cut | - | none | - |
| mooshroom | EntityList.java:96 | partial(render-only cow variant; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entity_skin_for_name | none | `/summon minecraft:mooshroom ~ ~ ~` beside a cow |
| snowman | EntityList.java:97 | missing | - | none | `/summon minecraft:snowman ~ ~ ~` on a snow platform |
| ocelot | EntityList.java:98 | cut | - | none | - |
| villager_golem | EntityList.java:99 | cut | - | none | - |
| horse | EntityList.java:100 | cut | - | none | - |
| rabbit | EntityList.java:101 | missing | - | none | `/summon minecraft:rabbit ~ ~ ~` in a fenced sand pen |
| polar_bear | EntityList.java:102 | cut | - | none | - |
| llama | EntityList.java:103 | partial(render-only; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | fast_s0_survival_default_rd8 | - |
| villager | EntityList.java:120 | cut | - | none | - |

## Projectiles and item-like entities

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| item | EntityList.java:1 | implemented | magma/game/live_sim.c:gm_live_tick_player; magma/game/item_render.c:gm_items_emit | fast_s0_survival_default_rd8, scenario_blaze_bow_demo, scenario_blaze_melee, scenario_ender_dragon, scenario_ender_dragon_demo, scenario_enderman_fight, scenario_smoke_zombie, scenario_wither_skeleton | - |
| xp_orb | EntityList.java:2 | implemented | magma/game/mob_live.c:gm_mobs_spawn_xp; magma/game/entity_render.c:gm_xp_orbs_emit | none | `/summon minecraft:xp_orb ~ ~1 ~ {Value:17}` and walk through it |
| area_effect_cloud | EntityList.java:3 | partial(tape damage effect only; no cloud entity render or live simulation) | magma/game/script.c:gm_script_run; magma/game/mob_live.c:gm_mobs_attack_player | scenario_dragon_kill_geared, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| egg | EntityList.java:7 | partial(render-only billboard; no live projectile simulation) | magma/game/entity_render.c:gm_entity_billboard_item; magma/game/item_render.c:gm_items_emit_billboard | none | `/summon minecraft:egg ~ ~2 ~ {Motion:[0.0d,0.0d,0.4d]}` across a dark wall |
| arrow | EntityList.java:10 | implemented | magma/game/runtime.c:tick_projectiles; magma/game/entity_render.c:gm_entities_emit | scenario_blaze_bow, scenario_blaze_bow_demo, scenario_fence_collide | - |
| snowball | EntityList.java:11 | partial(render-only billboard; no live projectile simulation) | magma/game/entity_render.c:gm_entity_billboard_item; magma/game/item_render.c:gm_items_emit_billboard | none | `/summon minecraft:snowball ~ ~2 ~ {Motion:[0.0d,0.0d,0.4d]}` across a dark wall |
| fireball | EntityList.java:12 | implemented | magma/game/runtime.c:spawn_hostile_projectiles; magma/game/runtime.c:tick_projectiles; magma/game/item_render.c:gm_items_emit_billboard | none | `/summon minecraft:fireball ~ ~3 ~ {direction:[0.0d,0.0d,0.1d],ExplosionPower:1}` in open Nether |
| small_fireball | EntityList.java:13 | implemented | magma/game/runtime.c:spawn_hostile_projectiles; magma/game/runtime.c:tick_projectiles; magma/game/item_render.c:gm_items_emit_billboard | scenario_blaze_bow, scenario_blaze_bow_demo, scenario_blaze_melee | - |
| ender_pearl | EntityList.java:14 | partial(render-only billboard; no live projectile simulation) | magma/game/entity_render.c:gm_entity_billboard_item; magma/game/item_render.c:gm_items_emit_billboard | none | `/summon minecraft:ender_pearl ~ ~2 ~ {Motion:[0.0d,0.0d,0.4d]}` across a pale wall |
| eye_of_ender_signal | EntityList.java:15 | implemented | magma/game/runtime.c:throw_eye_of_ender; magma/game/runtime.c:tick_projectiles; magma/game/item_render.c:gm_items_emit_billboard | none | `/give @p minecraft:ender_eye 1` then use it in the Overworld |
| potion | EntityList.java:16 | missing | - | none | `/summon minecraft:potion ~ ~2 ~ {Potion:{id:"minecraft:splash_potion",Count:1b}}` over a marked pad |
| xp_bottle | EntityList.java:17 | missing | - | none | `/summon minecraft:xp_bottle ~ ~2 ~ {Motion:[0.0d,-0.2d,0.0d]}` over a marked pad |
| wither_skull | EntityList.java:19 | missing | - | none | `/summon minecraft:wither_skull ~ ~2 ~ {direction:[0.0d,0.0d,0.1d]}` across a pale wall |
| fireworks_rocket | EntityList.java:22 | missing | - | none | `/summon minecraft:fireworks_rocket ~ ~1 ~ {LifeTime:30}` against the night sky |
| spectral_arrow | EntityList.java:24 | partial(render-only arrow geometry; no spectral mechanics) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:spectral_arrow ~ ~2 ~ {Motion:[0.0d,0.0d,1.0d]}` into a target |
| shulker_bullet | EntityList.java:25 | missing | - | none | `/summon minecraft:shulker_bullet ~ ~2 ~` in a glass chamber |
| dragon_fireball | EntityList.java:26 | partial(render-only tape ghost; no live dragon-fireball simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/item_render.c:gm_items_emit_billboard | scenario_dragon_kill_geared, scenario_ender_dragon, scenario_ender_dragon_demo | - |
| evocation_fangs | EntityList.java:33 | missing | - | none | `/summon minecraft:evocation_fangs ~ ~ ~ {Warmup:0}` on a marked floor |
| llama_spit | EntityList.java:104 | missing | - | none | `/summon minecraft:llama_spit ~ ~2 ~ {Motion:[0.0d,0.0d,0.4d]}` across a dark wall |

## Miscellaneous entities and vehicles

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| leash_knot | EntityList.java:8 | missing | - | none | `/summon minecraft:leash_knot ~ ~1 ~` beside a fence |
| painting | EntityList.java:9 | missing | - | none | `/summon minecraft:painting ~ ~1 ~ {Facing:2,Motive:"Kebab"}` against a wall |
| item_frame | EntityList.java:18 | missing | - | none | `/summon minecraft:item_frame ~ ~1 ~ {Facing:2,Item:{id:"minecraft:diamond",Count:1b}}` against a wall |
| tnt | EntityList.java:20 | missing | - | none | `/summon minecraft:tnt ~ ~1 ~ {Fuse:40}` beside a striped dirt-and-stone wall |
| falling_block | EntityList.java:21 | partial(render-only tape ghost; no live falling-block simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/item_render.c:gm_falling_blocks_emit | fast_s0_survival_default_rd8 | - |
| armor_stand | EntityList.java:30 | partial(render-only tape ghost; no live armor-stand simulation) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | scenario_fence_collide | - |
| commandblock_minecart | EntityList.java:40 | cut | - | none | - |
| boat | EntityList.java:41 | implemented | magma/game/mob_live.c:gm_mobs_place_boat; magma/game/mob_live.c:gm_mobs_tick; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:boat ~ ~1 ~` on a short water lane, then mount it |
| minecart | EntityList.java:42 | partial(render-only; minecart simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:minecart ~ ~1 ~` on a short rail line |
| chest_minecart | EntityList.java:43 | partial(render-only generic minecart; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:chest_minecart ~ ~1 ~` beside an empty minecart |
| furnace_minecart | EntityList.java:44 | partial(render-only generic minecart; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:furnace_minecart ~ ~1 ~` beside an empty minecart |
| tnt_minecart | EntityList.java:45 | partial(render-only generic minecart; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:tnt_minecart ~ ~1 ~` beside an empty minecart |
| hopper_minecart | EntityList.java:46 | partial(render-only generic minecart; simulation cut) | magma/game/entity_render.c:gm_entity_type_for_name; magma/game/entity_render.c:gm_entities_emit | none | `/summon minecraft:hopper_minecart ~ ~1 ~` beside an empty minecart |
| spawner_minecart | EntityList.java:47 | cut | - | none | - |
| ender_crystal | EntityList.java:200 | implemented | magma/game/dragon_live.c:gm_dragon_fill_views; magma/game/entity_render.c:gm_entities_emit | scenario_dragon_kill, scenario_dragon_kill_geared, scenario_ender_dragon, scenario_ender_dragon_demo | - |

CENSUS entities DONE rows=81
