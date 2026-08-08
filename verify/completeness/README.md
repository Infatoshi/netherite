# Save-fork completeness harness

This directory owns the registry-complete and arbitrary-save foundation for
`docs/COMPLETENESS.md`.

The Java oracle must be running on the requested qrl port. `capture` parks the
integrated server at a tick START boundary, performs Minecraft's real player
and Anvil save, flushes it, copies the complete save while the server remains
parked, writes the corresponding authoritative observation and fail-closed
capability report, verifies every copied file hash, and then unlocks Java.

```bash
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64

uv run --no-project python verify/completeness/save_fork.py selftest
uv run --no-project python verify/completeness/save_fork.py report
uv run --no-project python verify/completeness/save_fork.py \
  capture verify/completeness/out/S0 --port 25575
uv run --no-project python verify/completeness/save_fork.py \
  capture-pair verify/completeness/out/S0a verify/completeness/out/S0b \
  --port 25575
uv run --no-project python verify/completeness/save_fork.py \
  validate verify/completeness/out/S0
uv run --no-project python verify/completeness/anvil_semantic.py \
  compare verify/completeness/out/S0a/save verify/completeness/out/S0b/save
uv run --no-project python verify/completeness/anvil_to_capsule.py selftest
uv run --no-project python verify/completeness/test_chunk_bundle.py
uv run --no-project python verify/completeness/test_cold_chunk_store.py
uv run --no-project python verify/completeness/test_native_checkpoint.py
uv run --no-project python verify/completeness/comparator_gate.py selftest
uv run --no-project python verify/completeness/fixture_contract.py selftest
uv run --no-project python verify/completeness/reduce_failure.py selftest
uv run --no-project python verify/completeness/gap_audit.py --check
uv run --no-project python verify/completeness/gap_audit.py
uv run --no-project python verify/completeness/surface_registry_gate.py
uv run --no-project python verify/completeness/smelting_registry_gate.py
uv run --no-project python verify/completeness/furnace_fuel_gate.py
uv run --no-project --with pillow python \
  verify/completeness/recorder_private_gate.py
uv run --no-project python verify/completeness/save_order_gate.py
```

`session.lock` is the only excluded file. A snapshot without `level.dat`, an
Anvil region, the authoritative boundary, the fail-closed capability report,
or matching hashes is fatal. Existing output directories are never replaced.

`closure_queue.json` is the checked execution order for every strict row that
is currently `OPEN` or `PARTIAL`. The gap audit rejects omissions, duplicates,
already-closed entries, and stale rows, so the queue cannot silently report
completion by losing work.

`strict_closure_receipts.json` records receipt-backed closure after the base
table reached 38 direct `DONE` rows. Its 42 promotions and twelve canonical
open owners must partition every remaining row. A promoted row may delegate
only Cartesian or cross-system residual breadth to one canonical owner; its
own finite registry and transition boundary must remain in `gate.sh` with a
retained receipt. `strict_closure_gate.py` rejects missing evidence, duplicate
promotion, an unlisted owner, or any 92-row partition drift.

The schema-v2 snapshot also hashes `hidden_state.json`, which carries full
entity/tile NBT, loaded chunk/entity/tile order, both scheduled-tick queues and
tie IDs, RNG state, AI/helper/navigation state, executing-task order, clocks,
and process-global cursors needed to fork the parked boundary. It is still not
a claim that native supports every field. The generated capability and import
reports name each rejection rather than dropping it.

`capture-pair` keeps Java parked while both copies are flushed and copied. The
semantic comparator preserves typed NBT and list order, reports the first exact
path, and explicitly excludes only region allocator metadata, `session.lock`,
`level.dat_old` fallback history, and named `level.dat` save bookkeeping.

The neutral Anvil importer validates the snapshot, decodes block ID/Add/Data,
SkyLight, and BlockLight section arrays in vanilla y-z-x order, selects the
authoritative player NBT by UUID, and builds the existing native capsule input.
It writes checksummed cold stores for every persisted column in every present
dimension, plus a compact ordered active-chunk bundle when given
`normalize_reload_locked` output. The native pager applies exact block,
metadata, sky-light, block-light, biome, and height payloads on demand and
retains live block edits across toroidal eviction. On the canonical host the
fork gate attached all three dimensions of a 1,305-column evolved save and
verified the 289-column active radius at t=0 in under one second, without
per-cell JSON.

Native persistence has versioned, checksummed indexed world-column stores and
an ABI-fenced runtime checkpoint. The checkpoint retains all represented
player, inventory, entity, tile, queue, RNG, clock, statistics, and dynamic
array state; nested NBT blobs are deep-serialized rather than retaining
process pointers. Immutable generation directories are published by replacing
the slot's `current` pointer last, so an interrupted write leaves the prior
generation live. The windowed client exposes the same backend through ESC,
Save and Quit to Title, world selection, and Play Selected World.

`test_native_checkpoint.py` proves exact continuation from t=0 and a nonzero
tick in fresh processes, including future input/edit sequences, deterministic
reserialization, empty-world saves, statistics and arbitrary JSON round-trip,
tagged player/EntityItem/container/minecart inventories, and fail-closed
truncation/bit-flip or invalid-tag-reference controls.

`test_mixed_checkpoint_campaign.py` is AI-05's long native precursor: 103
entities across ordinary living, Armor Stand, minecart, End Crystal, and item
stores run under opposite authoritative loaded orders, fork at tick 600, and
continue byte-exactly through tick 1,200. It does not replace the required
real-Java campaign. `mixed_order_java_gate.py` locks that direct campaign: two
independent Java reloads and native match every represented tick and all raw
world horizons through tick 1,200 in both forward and reverse insertion order.

`living_capacity_census.py` is ENT-09's fail-closed architectural worklist.
Its normal mode reports the residual fixed living surface; `--json` emits
every field and source location, and `--strict` locks the generated 455-field
cold record, reviewed hot-page internals, and the absence of bypassing spawn
sites. `test_living_cold_slot` crosses the former 95-usable-slot boundary for
spawn, tick order, rendering, collision, effects, checkpoint continuation,
two-cold interactions, mating, llama caravans, and stable pig/horse/boat mount
identity. The same gate pins dynamic village enumeration and entity/leash frame
staging so consumers cannot silently return to the hot-page limit.
`run_native_soak.py` runs the
integrated native runtime in isolated, affinity-pinned, low-priority lanes and
records per-lane output hashes, peak RSS, faults, swaps, and cumulative client
time for the PERF-04 receipt.

The arbitrary ItemStack
tag corpus includes names/lore, enchantments, attributes, potion and firework
payloads, books, maps, nested BlockEntityTag content, Forge capability data,
all primitive array types, and adversarial user fields named `type`/`value`.
Real Java registry, merge, and pickup checks are:

```bash
uv run --no-project python magma/trace/test_item_registry.py --port 25600
uv run --no-project python magma/trace/test_item_merge.py --port 25600
uv run --no-project python magma/trace/test_item_pickup.py --port 25600
```

The registry gate covers all 392 registered non-air item IDs and verifies
native and Java split/reconstruction tag identity. Merge and pickup include
equal and unequal arbitrary nested tags as causal negative controls.
The surface registry gate extends that census from the initialized Java game
to all 236 block registrations and their callback/comparator overrides, all
392 item registrations and their gameplay-method overrides, 400 ordered
crafting recipes, 51 smelting recipes, 27 effects, 37 potion types, 81 loot
tables, and every concrete inventory container/GUI class. Its checked
`surface_registry_manifest.json` gives every row a unique fixture candidate
and owning TODO. Use `--live` for a fresh Java comparison and `--update --live`
only after a deliberate registry change.
`smelting_registry_gate.py` compiles the native table and constant-time live
lookup, compares all 51 rows with that real-Java census, exercises exact-meta
negative controls, and mutation-tests both comparison paths.
`furnace_fuel_gate.py` locks `getItemBurnTime` for all 392 initialized item
rows at metadata 0, 1, and 15 and `getSmeltingExperience` for all 16 metadata
values. Its checked manifest is captured from the real client with
`--update --live`; ordinary runs compile and compare both native lookups
without launching Java.
`test_native_save_slot.py` proves
repeated atomic generations, load/resave, state/world/statistics identity, and
corrupt/traversal rejection. `test_native_save_ui.sh` drives the actual SDL UI
under Xvfb through pause, save, world selection, reload, and a second save.
The real Java fork at 1/20/200 ticks is exact for every represented field and
raw block/light/height output. Java-compatible outward Anvil serialization is
not part of `SAVE-02`; native worlds deliberately use the fail-closed native
format while Java Anvil remains the import/oracle format.
Strict mode retains diagnostic artifacts but exits nonzero at the first
whole-save limitation. `--bounded` emits a focused artifact and the complete
limitation list for harness development.

```bash
uv run --no-project python verify/completeness/anvil_to_capsule.py create \
  verify/completeness/out/S0 verify/completeness/out/native_S0
uv run --no-project python verify/completeness/anvil_to_capsule.py create \
  verify/completeness/out/S0 verify/completeness/out/native_S0_bounded \
  --normalized verify/completeness/out/normalized_reload.json --bounded
```

The cold-reload runner installs the snapshot into an isolated Java run, skips
launch-config world mutation, normalizes the first post-load boundary, restores
the captured hidden cursors, and advances exact action clocks at selected
horizons. It repeats Java A/B by default and invokes the real neutral importer
for the native branch. Even when later unsupported fields reject continuation,
it runs the exact t=0 persisted-world block/light probe and records dimensions,
persisted/active column counts, and elapsed time in `fork_report.json`. Strict
fixtures require a paired-boundary/negative-control metadata contract. Until
native accepts the imported boundary it exits
2 with the first named rejection; the two `--allow-*` flags are diagnostic.
When the capsule itself is representable, the runner now executes native too:
it verifies represented state and raw world identity before the first tick,
runs the same action clock, compares every state tick and block/light/height at
each selected horizon, and reports both the earliest whole-save capability gap
and the earliest divergence underneath that fence. A known divergence is a
successful uncontracted diagnostic result, not a false parity claim; a strict
fixture's represented divergence and an importer/capsule failure exit nonzero
unless the documented rejection override applies.

```bash
uv run --no-project python verify/completeness/fork_runner.py \
  verify/completeness/out/S0 verify/completeness/out/fork_S0 \
  --allow-uncontracted --allow-native-reject
```

`comparator_gate.py` owns mutation-tested NBT/raw numeric/block/light/queue/
order/event/pixel comparators. `reduce_failure.py` preserves the first failure
fingerprint while reducing horizon, input prefix, cuboid, entity set, and tile
set. A zero-frame or zero-owned-pixel comparison is always fatal.

`recorder_private_gate.py` locks HAR-08's checked real-client observation.
Pass `--live --instance 99` to repeat it in an isolated oracle; add `--update`
only when intentionally replacing the checked metrics after a passing run.

`save_order_gate.py` locks SAVE-03's checked Java A/B/native forks. The five
cases cover forward/reverse entity and hopper order, a real entity-chunk
unload/reload cycle, independent loaded/tickable tile lists, exact loaded
chunk order, and a mounted player/minecart graph through 20 ticks. The staging
scripts write the strict fixtures; `fork_runner.py --restore-source-hidden
--keep-reload-topology` resumes their in-memory causal boundary while retaining
vanilla's cold-reload watcher topology.

`skeleton_trap_gate.py` compares the native horse runtime with the checked
120-tick real-Java golden. It covers 960 living-entity rows and 26 projectile
rows, including loaded update order, accepted A* wandering, targeting,
bow-strafe transitions, shooter identity, projectile age, and every represented
entity RNG cursor. Regenerate the compact checked golden only from a retained
full oracle trace with the explicit `reduce` command; the ordinary gate never
launches or mutates the Java oracle.

```bash
make -C magma game/test_horse_runtime
uv run --no-project python verify/completeness/skeleton_trap_gate.py
```

`callback_census.py` and `item_callback_census.py` fail closed while joining
every reflected block/item callback override to its exact pinned Java source
body. Their output is the generated WORLD-02/ITEM-08 behavior-family worklist;
it is classification evidence, not behavioral parity by itself.

```bash
uv run --no-project python verify/completeness/callback_census.py
uv run --no-project python verify/completeness/item_callback_census.py
```
